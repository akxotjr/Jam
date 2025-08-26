#include "pch.h"
#include "ShardExecutor.h"

#include "Clock.h"
#include "GlobalExecutor.h"
#include "WinFiberBackend.h"

namespace jam::utils::exec
{
	ShardExecutor::ShardExecutor(const ShardExecutorConfig& config, Wptr<GlobalExecutor> owner)
		: m_config(config), m_owner(std::move(owner))
	{
		m_scheduler = std::make_unique<thrd::FiberScheduler>(m_backend);
		m_shardsCtok = std::make_unique<moodycamel::ConsumerToken>(m_shardsQ);
		m_readyCtok = std::make_unique<moodycamel::ConsumerToken>(m_readyQ);
	}

	ShardExecutor::~ShardExecutor()
	{
		Stop();
		Join();
	}

	void ShardExecutor::Start()
	{
		if (m_running.exchange(true))
			return;

		m_thread = std::thread([this]()
			{
				if (m_pinEnabled)
					utils::sys::PinCurrentThreadTo(m_pinSlot);

				m_scheduler->AttachToCurrentThread();
				Loop();
				m_scheduler->DetachFromThread();
			});
	}

	void ShardExecutor::Stop()
	{
		if (!m_running.exchange(false))
			return;
		// 깨우기
		{
			auto& tok = TlsTokenFor(m_shardsQ);
			m_shardsQ.enqueue(tok, job::Job([] {}));
		}
		{
			auto& tok = TlsTokenFor(m_readyQ);
			m_readyQ.enqueue(tok, nullptr);

		}
	}

	void ShardExecutor::Join()
	{
		if (m_thread.joinable())
			m_thread.join();

		m_shardsCtok.reset();
		m_readyCtok.reset();
	}

	void ShardExecutor::Submit(job::Job job)
	{
		auto& tok = TlsTokenFor(m_shardsQ);
		m_shardsQ.enqueue(tok, std::move(job));
	}

	std::shared_ptr<Mailbox> ShardExecutor::CreateMailbox()
	{
		auto id = m_nextMailboxId.fetch_add(1, std::memory_order_relaxed);
		auto mb = std::make_shared<Mailbox>(id, weak_from_this());
		{
			WRITE_LOCK
			m_mailboxes.emplace(id, mb);
		}
		return mb;
	}

	void ShardExecutor::RemoveMailbox(uint32 id)
	{
		WRITE_LOCK
		m_mailboxes.erase(id);
	}

	void ShardExecutor::NotifyReady(Mailbox* mb)
	{
		// Mailbox가 처음 채워졌을 때 ready 큐에 등록
		auto& tok = TlsTokenFor(m_readyQ);
		m_readyQ.enqueue(tok, mb);
	}

	void ShardExecutor::SetPinSlot(const utils::sys::CoreSlot& slot)
	{
		m_pinSlot = slot;
		m_pinEnabled = true;
	}

	void ShardExecutor::SpawnFiber(thrd::FiberFn fn, const thrd::FiberDesc& desc)
	{
		m_scheduler->PostSpawn(std::move(fn), desc);
	}

	void ShardExecutor::ResumeFiber(thrd::AwaitKey key)
	{
		m_scheduler->PostResume(key);
	}

	void ShardExecutor::CancelFiberByKey(thrd::AwaitKey key, thrd::eCancelCode code)
	{
		m_scheduler->CancelByKey(key, code);
	}

	void ShardExecutor::CancelFiberById(uint32 id, thrd::eCancelCode code)
	{
		m_scheduler->CancelById(id, code);
	}

	void ShardExecutor::AssistDrainOnce(int32 maxMailboxes, int32 budgetPerMailbox)
	{
		int processedLists = 0;
		while (processedLists < maxMailboxes)
		{
			Mailbox* mb = nullptr;
			if (!m_readyQ.try_dequeue(*m_readyCtok, mb) || mb == nullptr)
				break;

			if (mb->TryBeginConsume())
			{
				ProcessMailbox(mb, budgetPerMailbox);
				mb->EndConsume();
			}

			// 아직 남아있다면 다시 ready에 넣어 재처리
			if (!mb->IsEmpty())
			{
				auto& tok = TlsTokenFor(m_readyQ);
				m_readyQ.enqueue(tok, mb);
			}

			++processedLists;
		}

		// backlog가 충분히 줄었으면 assist 요청 플래그 해제
		if (processedLists > 0 && m_readyQ.size_approx() == 0) // 비었거나 충분히 줄었을 때
			m_assistRequested.store(false, std::memory_order_relaxed);
	}

	void ShardExecutor::Loop()
	{
		while (m_running.load())
		{
			bool didWork = false;

			// 샤드 자체 작업
			for (int i = 0; i < 32; ++i)
			{
				job::Job j([] {});
				if (!m_shardsQ.try_dequeue(*m_shardsCtok, j))
					break;
				didWork = true;
				j.Execute();
			}

			// 준비된 Mailbox 처리
			didWork |= ProcessReadyOnce();

			m_scheduler->Poll(m_config.batchBudget, Clock::Instance().NowNs());

			if (!didWork)
				std::this_thread::sleep_for(std::chrono::milliseconds(m_config.idleSleepMs));
		}
	}

	bool ShardExecutor::ProcessReadyOnce()
	{
		bool didWork = false;

		Mailbox* mb = nullptr;
		if (!m_readyQ.try_dequeue(*m_readyCtok, mb) || mb == nullptr)
			return false;

		// 동시에 1 소비자 보장
		if (mb->TryBeginConsume())
		{
			ProcessMailbox(mb, m_config.batchBudget);
			mb->EndConsume();
			didWork = true;

			// 아직 남아있으면 재등록
			if (!mb->IsEmpty())
			{
				auto& tok = TlsTokenFor(m_readyQ);
				m_readyQ.enqueue(tok, mb);
			}

			// 임계치 체크
			RequestAssistIfNeeded(mb);
		}
		else
		{
			// 이미 다른 스레드가 처리 중이면 나중에 다시 시도
			auto& tok = TlsTokenFor(m_readyQ);
			m_readyQ.enqueue(tok, mb);
		}

		return didWork;
	}

	void ShardExecutor::ProcessMailbox(Mailbox* mb, int32 budget)
	{
		// bulk pop으로 배치 처리
		static thread_local xvector<job::Job> batch;
		batch.resize(budget);

		uint64 n = mb->TryPopBulk(batch.data(), batch.size());
		for (uint64 i = 0; i < n; ++i)
			batch[i].Execute();

		// bulk에서 덜 뽑혔으면 단건으로 마저 소비
		for (int32 i = static_cast<int32>(n); i < budget; ++i)
		{
			job::Job j([] {});
			if (!mb->TryPop(j)) break;
			j.Execute();
		}
	}

	void ShardExecutor::RequestAssistIfNeeded(Mailbox* mb)
	{
		if (mb->GetSizeApprox() >= m_config.assistThreshold)
		{
			bool expected = false;
			if (m_assistRequested.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
			{
				if (auto owner = m_owner.lock())
					owner->RequestAssist(static_cast<uint32>(m_config.index));
			}
		}
	}
}
