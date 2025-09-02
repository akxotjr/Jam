#include "pch.h"
#include "ShardExecutor.h"
#include "Job.h"
#include "Clock.h"
#include "GlobalExecutor.h"
#include "WinFiberBackend.h"
#include "ShardDirectory.h"

namespace jam::utils::exec
{
	ShardExecutor::ShardExecutor(const ShardExecutorConfig& config, Wptr<GlobalExecutor> owner)
			: m_config(config), m_owner(std::move(owner))
	{
		m_scheduler			= std::make_unique<thrd::FiberScheduler>(m_backend);
		m_shardsCtok		= std::make_unique<moodycamel::ConsumerToken>(m_shardsQ);
		m_readyCtrlCtok		= std::make_unique<moodycamel::ConsumerToken>(m_readyCtrlQ);
		m_readyNormalCtok	= std::make_unique<moodycamel::ConsumerToken>(m_readyNormalQ);
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
			auto& tok = TlsTokenFor(m_readyCtrlQ);
			m_readyCtrlQ.enqueue(tok, nullptr);
		}
		{
			auto& tok = TlsTokenFor(m_readyNormalQ);
			m_readyNormalQ.enqueue(tok, nullptr);
		}


		if (m_shardSlot)
		{
			for (uint8 i = 0; i < E2U(eMailboxChannel::COUNT); ++i)
			{
				auto& qs = m_shardSlot->ch[i];
				qs.state.store(E2U(eShardState::CLOSED), std::memory_order_release);
				qs.q.store(nullptr, std::memory_order_release);
				qs.gen.fetch_add(1, std::memory_order_acq_rel);  // 폐기 세대
			}
		}
	}

	void ShardExecutor::Join()
	{
		if (m_thread.joinable())
			m_thread.join();

		m_shardsCtok.reset();
		m_readyCtrlCtok.reset();
		m_readyNormalCtok.reset();
	}

	void ShardExecutor::Submit(job::Job job)
	{
		auto& tok = TlsTokenFor(m_shardsQ);
		m_shardsQ.enqueue(tok, std::move(job));
	}

	std::shared_ptr<Mailbox> ShardExecutor::CreateMailbox(eMailboxChannel channel)
	{
		auto id = m_nextMailboxId.fetch_add(1, std::memory_order_relaxed);
		auto mb = std::make_shared<Mailbox>(id, weak_from_this(), channel);
		{
			WRITE_LOCK
			m_mailboxes.emplace(id, mb);
		}

		// 채널 ingress Mailbox 게시
		if (m_shardSlot)
		{
			auto& qs = m_shardSlot->ch[E2U(channel)];
			qs.state.store(E2U(eShardState::CLOSED), std::memory_order_release);
			qs.q.store(mb.get(), std::memory_order_release);
			qs.gen.fetch_add(1, std::memory_order_acq_rel);      // 새 세대
			qs.state.store(E2U(eShardState::OPEN), std::memory_order_release);
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
		auto& q = (mb->GetMailboxChannel() == eMailboxChannel::CTRL) ? m_readyCtrlQ : m_readyNormalQ;
		auto& tok = TlsTokenFor(q);
		q.enqueue(tok, mb);
	}

	void ShardExecutor::PinCoreSlot(const utils::sys::CoreSlot& slot)
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

	void ShardExecutor::OnGroupLocalJoin(uint64 group_id, std::shared_ptr<Mailbox> mailbox)
	{
		auto& gl = m_groupLocal[group_id];
		gl.members.emplace_back(mailbox);
	}

	void ShardExecutor::OnGroupLocalLeave(uint64 group_id, std::shared_ptr<Mailbox> mailbox)
	{
		auto it = m_groupLocal.find(group_id);
		if (it == m_groupLocal.end()) return;

		auto& v = it->second.members;
		// 약한참조가 아니므로 직접 비교해 제거 (약한참조로 바꾸려면 lock()해서 비교)
		for (size_t i = 0; i < v.size(); )
		{
			if (v[i].expired() || v[i].lock() == mailbox) 
			{
				v[i] = v.back();
				v.pop_back();
			}
			else 
			{
				++i;
			}
		}
		if (v.empty()) m_groupLocal.erase(it);
	}

	void ShardExecutor::OnGroupHomeMark(uint64 group_id, uint32 shardIdx, int32 delta)
	{
		auto& gh = m_groupHome[group_id];
		if (gh.shard_refcnt.size() <= shardIdx)
			gh.shard_refcnt.resize(shardIdx + 1, 0);

		int64 newv = (int64)gh.shard_refcnt[shardIdx] + (int64)delta;
		if (newv < 0) newv = 0;
		gh.shard_refcnt[shardIdx] = (uint32)newv;

		// (옵션) 모두 0이면 gh를 지울 수도 있음
		// bool all0 = std::all_of(gh.shard_refcnt.begin(), gh.shard_refcnt.end(), [](uint32 x){return x==0;});
		// if (all0) m_groupHome.erase(group_id);
	}

	void ShardExecutor::OnGroupMulticastHome(uint64 group_id, job::Job j)
	{
		// 1) 홈 샤드 로컬 멤버에게 배달
		DeliverToLocal(group_id, j);

		// 2) 원격 샤드 forward
		auto it = m_groupHome.find(group_id);
		if (it == m_groupHome.end()) return;

		const auto& refcnt = it->second.shard_refcnt;
		if (refcnt.empty()) return;

		// owner(GlobalExecutor) 통해 원격 샤드 Sptr 얻기
		auto owner = m_owner.lock();
		if (!owner) return;

		for (uint32 s = 0; s < refcnt.size(); ++s)
		{
			if (s == (uint32)m_config.index) continue;
			if (refcnt[s] == 0) continue;

			auto remote = owner->GetShard(s); // GlobalExecutor에 이 API가 있어야 함
			if (!remote) continue;

			// 실행자 기반 엔드포인트로 직접 Post
			ShardEndpoint ep(remote);
			ep.Post(job::Job([remote, group_id, j] {
				remote->OnGroupMulticastRemote(group_id, j);
				}));
		}
	}

	void ShardExecutor::OnGroupMulticastRemote(uint64 group_id, job::Job j)
	{
		DeliverToLocal(group_id, j);
	}

	void ShardExecutor::DeliverToLocal(uint64 group_id, job::Job j)
	{
		auto it = m_groupLocal.find(group_id);
		if (it == m_groupLocal.end()) return;

		auto& members = it->second.members;
		size_t i = 0;
		while (i < members.size())
		{
			if (auto q = members[i].lock())
			{
				// 세션 Mailbox로 최종 배달(복사비 줄이려면 capturable payload로)
				q->Post(j);
				++i;
			}
			else
			{
				members[i] = members.back();
				members.pop_back();
			}
		}
	}

	void ShardExecutor::Tick(uint64 now_ns, uint64 dt_ns)
	{
		auto& L = m_local;

		// 1) Systems run in fixed order
		for (auto* fn : L.systems) {
			fn(L, now_ns, dt_ns);
		}

		// 2) 프레임 말미 지연 작업 일괄 반영
		if (!L.defers.empty()) {
			for (auto& f : L.defers) f(L.world);
			L.defers.clear();
		}

		// 3)
		L.events.update();
	}


	void ShardExecutor::BeginDrain()
	{
		if (!m_shardSlot)
			return;

		for (uint8 i = 0; i < E2U(eMailboxChannel::COUNT); ++i)
			m_shardSlot->ch[i].state.store(E2U(eShardState::DRAINING), std::memory_order_release);
	}

	void ShardExecutor::AssistDrainOnce(int32 maxMailboxes, int32 budgetPerMailbox)
	{
		int processedLists = 0;
		while (processedLists < maxMailboxes)
		{
			Mailbox* mb = nullptr;
			if (!TryDequeueReady(mb) || mb == nullptr)
				break;

			if (mb->TryBeginConsume())
			{
				ProcessMailbox(mb, budgetPerMailbox);
				mb->EndConsume();
			}

			// 아직 남아있다면 다시 ready에 넣어 재처리
			if (!mb->IsEmpty())
				NotifyReady(mb);

			++processedLists;
		}

		if (processedLists > 0 && m_readyCtrlQ.size_approx() == 0 && m_readyNormalQ.size_approx() == 0)
		{
			m_assistRequested.store(false, std::memory_order_relaxed);
		}
	}

	void ShardExecutor::Loop()
	{
		while (m_running.load())
		{
			bool didWork = false;

			// 샤드 자체 작업
			for (int i = 0; i < 32; ++i)	// why 32 ?
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
		if (!TryDequeueReady(mb) || mb == nullptr)
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
				NotifyReady(mb);
			}

			// 임계치 체크
			RequestAssistIfNeeded(mb);
		}
		else
		{
			NotifyReady(mb);
		}

		return didWork;
	}

	void ShardExecutor::ProcessMailbox(Mailbox* mb, int32 budget)
	{
		// bulk pop으로 배치 처리
		static thread_local std::vector<job::Job> batch;
		batch.clear();
		batch.reserve(budget);

		uint64 n = mb->TryPopBulk(std::back_inserter(batch), static_cast<uint64>(budget));

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


	bool ShardExecutor::TryDequeueReady(OUT Mailbox*& mailbox)
	{
		// 1) Ctrl 
		if (m_readyCtrlQ.try_dequeue(*m_readyCtrlCtok, mailbox))
			return true;

		// 2) Normal
		if (m_readyNormalQ.try_dequeue(*m_readyNormalCtok, mailbox))
			return true;

		return false;
	}
}
