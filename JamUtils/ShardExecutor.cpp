#include "pch.h"
#include "ShardExecutor.h"
#include "GlobalExecutor.h"

namespace jam::utils::exec
{
	ShardExecutor::ShardExecutor(const ShardExecutorConfig& config, Wptr<GlobalExecutor> owner)
		: m_config(config), m_owner(std::move(owner))
	{
		m_scheduler = make_unique<thrd::FiberScheduler>();
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

				Loop();
			});
	}

	void ShardExecutor::Stop()
	{
		if (!m_running.exchange(false))
			return;
		// �����
		m_queue.enqueue(job::Job([] {}));
		m_ready.enqueue(nullptr);
	}

	void ShardExecutor::Join()
	{
		if (m_thread.joinable())
			m_thread.join();
	}

	void ShardExecutor::Submit(job::Job job)
	{
		m_queue.enqueue(std::move(job));
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
		// Mailbox�� ó�� ä������ �� ready ť�� ���
		m_ready.enqueue(mb);
	}

	void ShardExecutor::SetPinSlot(const utils::sys::CoreSlot& slot)
	{
		m_pinSlot = slot;
		m_pinEnabled = true;
	}

	void ShardExecutor::AssistDrainOnce(int32 maxMailboxes, int32 budgetPerMailbox)
	{
		int processedLists = 0;
		while (processedLists < maxMailboxes)
		{
			Mailbox* mb = nullptr;
			if (!m_ready.try_dequeue(mb) || mb == nullptr)
				break;

			if (mb->TryBeginConsume())
			{
				ProcessMailbox(mb, budgetPerMailbox);
				mb->EndConsume();
			}

			// ���� �����ִٸ� �ٽ� ready�� �־� ��ó��
			if (!mb->IsEmpty())
				m_ready.enqueue(mb);

			++processedLists;
		}

		// backlog�� ����� �پ����� assist ��û �÷��� ����
		if (processedLists > 0 && !m_ready.peek()) // ����ų� ����� �پ��� ��
			m_assistRequested.store(false, std::memory_order_relaxed);
	}

	void ShardExecutor::Loop()
	{
		while (m_running.load())
		{
			bool didWork = false;

			// ���� ��ü �۾�
			for (int i = 0; i < 32; ++i)
			{
				job::Job j([] {});
				if (!m_queue.try_dequeue(j))
					break;
				didWork = true;
				j.Execute();
			}

			// �غ�� Mailbox ó��
			didWork |= ProcessReadyOnce();

			if (!didWork)
				std::this_thread::sleep_for(std::chrono::milliseconds(m_config.idleSleepMs));
		}
	}

	bool ShardExecutor::ProcessReadyOnce()
	{
		bool didWork = false;

		Mailbox* mb = nullptr;
		if (!m_ready.try_dequeue(mb) || mb == nullptr)
			return false;

		// ���ÿ� 1 �Һ��� ����
		if (mb->TryBeginConsume())
		{
			ProcessMailbox(mb, m_config.batchBudget);
			mb->EndConsume();
			didWork = true;

			// ���� ���������� ����
			if (!mb->IsEmpty())
				m_ready.enqueue(mb);

			// �Ӱ�ġ üũ
			RequestAssistIfNeeded(mb);
		}
		else
		{
			// �̹� �ٸ� �����尡 ó�� ���̸� ���߿� �ٽ� �õ�
			m_ready.enqueue(mb);
		}

		return didWork;
	}

	void ShardExecutor::ProcessMailbox(Mailbox* mb, int32 budget)
	{
		// bulk pop���� ��ġ ó��
		static thread_local xvector<job::Job> batch;
		batch.resize(budget);

		uint64 n = mb->TryPopBulk(batch.data(), batch.size());
		for (uint64 i = 0; i < n; ++i)
			batch[i].Execute();

		// bulk���� �� �������� �ܰ����� ���� �Һ�
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
