#include "pch.h"
#include "jamnet/core/executor/ShardExecutor.h"
#include "jamnet/core/executor/Job.h"
#include "jamnet/core/executor/GlobalExecutor.h"
#include "jamnet/core/executor/ShardTLS.h"
#include "jamnet/core/executor/ThreadRegistry.h"

namespace jam
{
	ShardExecutor::ShardExecutor(const ShardExecutorConfig& config)
			: m_config(config)
	{
		m_scheduler	= std::make_unique<FiberScheduler>(m_backend);
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

		m_local.scheduler = m_scheduler.get();

		m_thread = std::thread([this]()
			{
				std::string threadName = std::format("ShardExecutor#{}", m_config.index);
				ThreadRegistry::InitExecutorThread(threadName, this);

				if (m_pinEnabled)
					PinCurrentThreadTo(m_pinSlot);

				try 
				{
					ShardTLS::Bind(&m_local, std::this_thread::get_id());
				}
				catch (const std::exception& ex) 
				{
					std::cerr << "[ShardExecutor] Bind failed: " << ex.what() << "\n";
					return;
				}

				m_scheduler->AttachToCurrentThread();
				Loop();
				m_scheduler->DetachFromThread();

				ShardTLS::Unbind();
			});
	}

	void ShardExecutor::Stop()
	{
		if (!m_running.exchange(false))
			return;

		m_local.scheduler = nullptr;

		if (m_shardSlot)
		{
			auto& qs = m_shardSlot->inbox;
			qs.state.store(E2U(eShardState::CLOSED), std::memory_order_release);
			qs.q.store(nullptr, std::memory_order_release);
			qs.gen.fetch_add(1, std::memory_order_acq_rel);
		}
	}

	void ShardExecutor::Join()
	{
		if (m_thread.joinable())
			m_thread.join();
	}

	void ShardExecutor::Submit(Job j)
	{
		m_jobIngress.enqueue(std::move(j));
	}

	void ShardExecutor:: SubmitAfter(Job j, uint64 delay_ns)
	{
		if (!m_scheduler) 
		{
			Submit(std::move(j));
			return;
		}

		const uint64 deadline = NOW_NS() + delay_ns;

		m_scheduler->PostSpawn(
			[this, deadline, j = std::move(j)]() mutable
			{
				m_scheduler->SleepUntil(deadline);
				Submit(std::move(j));
			},
			FiberDesc{ .name = "Shard.SubmitAfter" }
		);
	}

	std::shared_ptr<Mailbox> ShardExecutor::CreateMailbox()
	{
		auto id = m_nextMailboxId.fetch_add(1, std::memory_order_relaxed);
		auto mb = std::make_shared<Mailbox>(id, weak_from_this());
		{
			WRITE_LOCK
			m_mailboxes.emplace(id, mb);
		}

		if (m_shardSlot)
		{
			auto& qs = m_shardSlot->inbox;
			qs.state.store(E2U(eShardState::CLOSED), std::memory_order_release);
			qs.q.store(mb.get(), std::memory_order_release);
			qs.gen.fetch_add(1, std::memory_order_acq_rel);
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
		if (!mb) return;

		m_readyMailboxes.enqueue(mb);
	}

	void ShardExecutor::PinCoreSlot(const CoreSlot& slot)
	{
		m_pinSlot = slot;
		m_pinEnabled = true;
	}

	void ShardExecutor::SpawnFiber(FiberFn fn, const FiberDesc& desc)
	{
		m_scheduler->PostSpawn(std::move(fn), desc);
	}

	void ShardExecutor::ResumeFiber(AwaitKey key)
	{
		m_scheduler->PostResume(key);
	}

	void ShardExecutor::CancelFiberByKey(AwaitKey key, eCancelCode code)
	{
		m_scheduler->PostCancelByKey(key, code);
	}

	void ShardExecutor::CancelFiberById(uint32 id, eCancelCode code)
	{
		m_scheduler->PostCancelById(id, code);
	}

	void ShardExecutor::Tick(uint64 now_ns, uint64 dt_ns)
	{
		auto& L = m_local;
		
		// Domain groups tick
		for (auto& group : L.domainGroups | views::values)
		{
			const uint64 period_ns = group.tickPeriod_ns;

			// tick 주기 체크 (0이면 매번 실행)
			if (period_ns != 0)
			{
				const uint64 elapsed_ns = now_ns - group.lastTick_ns;
				if (elapsed_ns < period_ns)
					continue;

				// dt는 "그룹 기준 dt"로 재계산
				dt_ns = elapsed_ns;
			}

			group.lastTick_ns = now_ns;

			// Bootstrap (한 번만 실행)
			if (!group.bootstraps.empty())
			{
				auto bootstraps = std::move(group.bootstraps);
				group.bootstraps.clear();

				for (auto& fn : bootstraps)
				{
					if (fn) fn(L);
				}
			}

			// Systems
			// (필요하면 systemsCopy 전략으로 바꿀 수 있으나, 현재는 직접 순회)
			for (auto& fn : group.systems)
			{
				if (fn) fn(L, now_ns, dt_ns);
			}
		}

		// Defers (registry에 대한 지연 작업)
		if (!L.defers.empty())
		{
			auto defers = std::move(L.defers);
			L.defers.clear();

			for (auto& f : defers)
			{
				if (f) f(L.registry);
			}
		}
	}



	void ShardExecutor::BeginDrain()
	{
		if (!m_shardSlot)
			return;

		m_shardSlot->inbox.state.store(E2U(eShardState::DRAINING), std::memory_order_release);
	}

	ShardExecutor::PeriodicHandle ShardExecutor::ScheduleFixedRate(Job j, const PeriodicOptions& opt)
	{
		auto st				= std::make_shared<PeriodicState>();
		st->period_ns		= opt.period_ns;
		st->initialDelay_ns = opt.initialDelay_ns;
		st->maxCatchUp		= opt.maxCatchUp;
		st->fixedRate		= true;

		const uint32 id = m_periodicId.fetch_add(1, std::memory_order_relaxed);
		{
			WRITE_LOCK
			m_periodics[id] = st;
		}

		const char* fiberName = opt.name ? opt.name : "Shard.PeriodicFixedRate";

		m_scheduler->PostSpawn(
			[this, st, j, id]()
			{
				uint64 next_ns = NOW_NS() + st->initialDelay_ns;
				const uint64 period_ns = st->period_ns;

				while (m_running.load(std::memory_order_acquire) && !st->cancelled.load(std::memory_order_relaxed))
				{
					m_scheduler->SleepUntil(next_ns);
					if (!m_running.load(std::memory_order_acquire) || st->cancelled.load(std::memory_order_relaxed))
						break;

					Submit(j);

					next_ns += period_ns;

					int32 catches = 0;
					for (uint64 now_ns = NOW_NS(); now_ns >= next_ns && catches < st->maxCatchUp; now_ns = NOW_NS(), ++catches)
					{
						if (!m_running.load(std::memory_order_acquire) || st->cancelled.load(std::memory_order_relaxed))
							break;
						Submit(j);
						next_ns += period_ns;
					}
				}

				WRITE_LOCK
				m_periodics.erase(id);
			},
			FiberDesc{ .name = fiberName }
		);

		return PeriodicHandle{ id };
	}

	ShardExecutor::PeriodicHandle ShardExecutor::ScheduleFixedDelay(Job j, const PeriodicOptions& opt)
	{
		auto st = std::make_shared<PeriodicState>();
		st->period_ns		= opt.period_ns;
		st->initialDelay_ns = opt.initialDelay_ns;
		st->maxCatchUp		= 0;
		st->fixedRate		= false;

		const uint32 id = m_periodicId.fetch_add(1, std::memory_order_relaxed);
		{
			WRITE_LOCK
			m_periodics[id] = st;
		}

		const char* fiberName = opt.name ? opt.name : "Shard.PeriodicFD";

		m_scheduler->PostSpawn(
			[this, st, j, id]()
			{
				uint64 next = NOW_NS() + st->initialDelay_ns;
				m_scheduler->SleepUntil(next);

				while (m_running.load(std::memory_order_acquire) && !st->cancelled.load(std::memory_order_relaxed))
				{
					Submit(j);
					if (!m_running.load(std::memory_order_acquire) || st->cancelled.load(std::memory_order_relaxed))
						break;

					m_scheduler->SleepUntil(NOW_NS() + st->period_ns);
				}

				WRITE_LOCK
				m_periodics.erase(id);
			},
			FiberDesc{ .name = fiberName }
		);

		return PeriodicHandle{ id };
	}

	bool ShardExecutor::CancelPeriodic(PeriodicHandle h)
	{
		WRITE_LOCK
		auto it = m_periodics.find(h.id);
		if (it == m_periodics.end()) return false;
		if (auto st = it->second.lock())
		{
			st->cancelled.store(true, std::memory_order_relaxed);
			return true;
		}
		return false;
	}

	void ShardExecutor::Loop()
	{
		while (m_running.load())
		{
			bool didWork = false;

			DrainReadyMailboxes(64, m_config.batchBudget);

			didWork |= ProcessJobsOnce();

			m_scheduler->Poll(m_config.batchBudget, NOW_NS());

			uint64 now_ns = NOW_NS();
			uint64 elapsed_ns = now_ns - m_lastTick_ns;
			if (elapsed_ns >= m_config.tickPeriod_ns)
			{
				uint64 dt = elapsed_ns;
				Tick(now_ns, dt);
				m_lastTick_ns = now_ns;
				didWork = true;
			}

			if (!didWork)
				std::this_thread::sleep_for(std::chrono::milliseconds(m_config.idleSleepMs));
		}
	}


	void ShardExecutor::PushLocal(Job&& j)
	{
		const size_t idx = static_cast<size_t>(j.Priority());
		if (idx >= kPrioCount)
			return;

		m_jobLocalByPrio[idx].push_back(std::move(j));
		++m_jobLocalTotal;
	}

	bool ShardExecutor::TryPopLocal(Job& j)
	{
		if (m_jobLocalTotal == 0)
			return false;

		for (size_t i = 0; i < kPrioCount; ++i)
		{
			auto& q = m_jobLocalByPrio[i];
			if (q.empty())
				continue;

			j = std::move(q.back());
			q.pop_back();
			--m_jobLocalTotal;
			return true;
		}

		// total과 실제가 불일치하면 보정
		m_jobLocalTotal = 0;
		return false;
	}

	bool ShardExecutor::DrainIngressOnce(int32 budget)
	{
		if (budget <= 0)
			return false;

		thread_local std::vector<Job> batch;
		batch.clear();
		batch.resize(static_cast<size_t>(budget));

		const size_t n = m_jobIngress.try_dequeue_bulk(batch.data(), batch.size());
		if (n == 0)
			return false;

		for (size_t i = 0; i < n; ++i)
		{
			PushLocal(std::move(batch[i]));
		}

		return true;
	}

	bool ShardExecutor::ProcessJobsOnce()
	{
		bool didWork = false;

		// 1) 외부 Submit()가 넣은 ingress를 먼저 로컬로 땡김
		didWork |= DrainIngressOnce(m_config.batchBudget);

		// 2) 로컬 큐가 비면 한 번 더 기회(타이밍 경합 완화)
		if (m_jobLocalTotal == 0)
			didWork |= DrainIngressOnce(m_config.batchBudget);

		// 3) 실행
		for (int32 i = 0; i < m_config.batchBudget; ++i)
		{
			Job j;
			if (!TryPopLocal(j))
				break;

			j.Execute();
			didWork = true;
		}

		return didWork;
	}

	void ShardExecutor::ProcessMailbox(Mailbox* mb, int32 budget)
	{
		thread_local std::vector<Job> batch;

		batch.clear();
		batch.resize(static_cast<size_t>(budget));

		const uint64 n = mb->TryDequeueBulk(std::span<Job>(batch.data(), batch.size()));
		if (n != 0)
		{
			for (uint64 i = 0; i < n; ++i)
			{
				PushLocal(std::move(batch[static_cast<size_t>(i)]));
			}
		}

		mb->EndConsume();

		if (!mb->IsEmpty() && mb->TryBeginConsume())
		{
			m_readyMailboxes.enqueue(mb);
		}
	}


	void ShardExecutor::DrainReadyMailboxes(int32 maxMailboxes, int32 budgetPerMailbox)
	{
		Mailbox* mb = nullptr;

		for (int32 i = 0; i < maxMailboxes; ++i)
		{
			if (!m_readyMailboxes.try_dequeue(mb))
				break;
			if (!mb)
				continue;

			ProcessMailbox(mb, budgetPerMailbox);
		}
	}
}
