#include "pch.h"
#include "jamnet/core/executor/ShardExecutor.h"
#include "jamnet/core/executor/Job.h"
#include "jamnet/core/executor/GlobalExecutor.h"
#include "jamnet/core/executor/ShardTLS.h"
#include "jamnet/core/executor/ThreadRegistry.h"

namespace jam
{
	namespace
	{
		struct ShardMetricsSnapshotRequest
		{
			ShardExecutorMetrics snapshot{};
			bool done = false;
		};

		struct ShardMetricsResetRequest
		{
			bool done = false;
		};
	}

	ShardExecutor::ShardExecutor(const ShardExecutorConfig& config)
			: m_config(config)
	{
		m_scheduler	= std::make_unique<FiberScheduler>(m_backend);
		m_metrics.shardIndex = m_config.index;
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

		{
			std::scoped_lock guard(m_metricSyncMutex);
			m_loopExited = false;
		}

		m_local.scheduler = m_scheduler.get();

		m_thread = std::thread([this]()
			{
				auto markLoopExited = [this]()
					{
						{
							std::scoped_lock guard(m_metricSyncMutex);
							m_loopExited = true;
						}
						m_metricSyncCv.notify_all();
					};

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
					markLoopExited();
					return;
				}

				m_scheduler->AttachToCurrentThread();
				Loop();
				m_scheduler->DetachFromThread();

				ShardTLS::Unbind();
				markLoopExited();
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
			qs.state.store(E2U(eShardState::Closed), std::memory_order_release);
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
			qs.state.store(E2U(eShardState::Closed), std::memory_order_release);
			qs.q.store(mb.get(), std::memory_order_release);
			qs.gen.fetch_add(1, std::memory_order_acq_rel);
			qs.state.store(E2U(eShardState::Open), std::memory_order_release);
		}

		return mb;
	}

	bool ShardExecutor::CloseMailbox(uint32 id, eMailboxCloseMode mode, std::function<void()> onClosed)
	{
		auto mailbox = FindMailbox(id);
		if (!mailbox)
			return false;

		return mailbox->Close(mode, std::move(onClosed));
	}

	void ShardExecutor::RemoveMailbox(uint32 id)
	{
		WRITE_LOCK
		m_mailboxes.erase(id);
	}

	void ShardExecutor::NotifyReady(uint32 mailboxId)
	{
		if (mailboxId == 0)
			return;

		m_readyMailboxes.enqueue(mailboxId);
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

	void ShardExecutor::ResumeFiber(FiberAwaitKey key)
	{
		m_scheduler->PostResume(key);
	}

	void ShardExecutor::CancelFiberByKey(FiberAwaitKey key, eCancelCode code)
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
		for (auto& group : L.domainGroups | std::views::values)
		{
			const uint64 period_ns = group.tickPeriod_ns;
			uint64		 group_dt  = dt_ns;

			// tick 주기 체크 (0이면 매번 실행)
			if (period_ns != 0)
			{
				if (group.lastTick_ns == 0)
					group.lastTick_ns = now_ns - period_ns;

				if (now_ns < group.lastTick_ns + period_ns)
					continue;

				group_dt = period_ns;
				group.lastTick_ns += period_ns;

				if (now_ns >= group.lastTick_ns + period_ns)
					group.lastTick_ns = now_ns;
			}
			else
			{
				group.lastTick_ns = now_ns;
			}

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
			for (auto& fn : group.systems)
			{
				if (fn) fn(L, now_ns, group_dt);
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

		m_shardSlot->inbox.state.store(E2U(eShardState::Draining), std::memory_order_release);
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
		m_lastTick_ns = NOW_NS();

		while (m_running.load())
		{
			++m_metrics.loopCount;
			bool didWork = false;

			DrainReadyMailboxes(64, m_config.batchBudget);
			didWork |= ProcessJobsOnce();

			const uint64 pollStart_ns = NOW_NS();
			
			m_scheduler->Poll(m_config.batchBudget, pollStart_ns);

			const uint64 pollCost_ns = NOW_NS() - pollStart_ns;
			++m_metrics.schedulerPollCount;
			m_metrics.schedulerPollCost_ns += pollCost_ns;

			const auto& fiberMetrics = m_scheduler->Profile();
			if (fiberMetrics.lastPollCost_ns != 0 && fiberMetrics.lastPollReadyRunCount == 0)
				++m_metrics.schedulerEmptyPollCount;
			m_metrics.schedulerReadyRunCount = fiberMetrics.readyRunCount;

			if (fiberMetrics.lastPollReadyRunCount > 0)
				didWork = true;

			const uint64 now_ns		= NOW_NS();
			const uint64 tickPeriod = m_config.tickPeriod_ns;

			if (now_ns >= m_lastTick_ns + tickPeriod)
			{
				int32 catches = 0;

				while (now_ns >= m_lastTick_ns + tickPeriod && catches < m_config.maxTickCatchUp)
				{
					m_lastTick_ns += tickPeriod;
					Tick(m_lastTick_ns, tickPeriod);
					++catches;
				}

				m_metrics.tickCount += static_cast<uint64>(catches);
				if (catches > 1)
					m_metrics.tickCatchUpCount += static_cast<uint64>(catches - 1);

				if (now_ns >= m_lastTick_ns + tickPeriod)
					m_lastTick_ns = now_ns;

				didWork = true;
			}

			if (!didWork)
           {
				++m_metrics.idleLoopCount;
				const uint64 idleStart_ns = NOW_NS();
				std::this_thread::sleep_for(std::chrono::milliseconds(m_config.idleSleepMs));
				m_metrics.idleSleepCost_ns += NOW_NS() - idleStart_ns;
			}
			else
			{
				++m_metrics.didWorkLoopCount;
			}
		}
	}


	void ShardExecutor::PushLocal(Job&& j)
	{
		const size_t idx = static_cast<size_t>(j.Priority());
		if (idx >= kPrioCount)
			return;

		m_jobLocalByPrio[idx].push_back(std::move(j));
		m_jobLocalTotal.fetch_add(1, std::memory_order_relaxed);
	}

	bool ShardExecutor::TryPopLocal(Job& j)
	{
		if (m_jobLocalTotal.load(std::memory_order_relaxed) == 0)
			return false;

		for (size_t i = 0; i < kPrioCount; ++i)
		{
			auto& q = m_jobLocalByPrio[i];
			if (q.empty())
				continue;

			j = std::move(q.back());
			q.pop_back();
			m_jobLocalTotal.fetch_sub(1, std::memory_order_relaxed);
			return true;
		}

		// total과 실제가 불일치하면 보정
		m_jobLocalTotal.store(0, std::memory_order_relaxed);
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

		++m_metrics.ingressBatchCount;
		m_metrics.ingressJobCount += static_cast<uint64>(n);

		for (size_t i = 0; i < n; ++i)
		{
			PushLocal(std::move(batch[i]));
		}

		return true;
	}

	bool ShardExecutor::ProcessJobsOnce()
	{
		++m_metrics.processJobsCallCount;
		bool didWork = false;
		uint64 execCount = 0;

		// 1) 외부 Submit()가 넣은 ingress를 먼저 로컬로 땡김
		didWork |= DrainIngressOnce(m_config.batchBudget);

		// 2) 로컬 큐가 비면 한 번 더 기회(타이밍 경합 완화)
		if (m_jobLocalTotal.load(std::memory_order_relaxed) == 0)
			didWork |= DrainIngressOnce(m_config.batchBudget);

		// 3) 실행
		for (int32 i = 0; i < m_config.batchBudget; ++i)
		{
			Job j;
			if (!TryPopLocal(j))
				break;

			j.Execute();
			didWork = true;
            ++execCount;
		}

		m_metrics.processJobsExecCount += execCount;

		return didWork;
	}

	void ShardExecutor::ProcessMailbox(const std::shared_ptr<Mailbox>& mb, int32 budget)
	{
		if (!mb) return;

		thread_local std::vector<Job> batch;

		batch.clear();
		batch.resize(static_cast<size_t>(budget));

		if (mb->ShouldAbortPending())
		{
			mb->DiscardPending();
		}
		else
		{
			const uint64 n = mb->TryDequeueBulk(std::span<Job>(batch.data(), batch.size()));
			if (n != 0)
			{
				mb->OnDequeuedForExecution(n);
				m_metrics.mailboxJobMoveCount += n;
				for (uint64 i = 0; i < n; ++i)
				{
					Job original = std::move(batch[static_cast<size_t>(i)]);
					const eJobPriority priority = original.Priority();
					PushLocal(Job([mb, original = std::move(original)]() mutable
						{
							if (!mb->ShouldAbortPending())
								original.Execute();

							mb->OnJobExecuted();
						}, priority));
				}
			}
		}

		mb->EndConsume();

		if (mb->TryFinalizeClose())
		{
			RemoveMailbox(mb->GetId());
			return;
		}

		if (!mb->IsEmpty() && mb->TryBeginConsume())
		{
			m_readyMailboxes.enqueue(mb->GetId());
		}
	}


	void ShardExecutor::DrainReadyMailboxes(int32 maxMailboxes, int32 budgetPerMailbox)
	{
		uint32 mailboxId = 0;

		for (int32 i = 0; i < maxMailboxes; ++i)
		{
			if (!m_readyMailboxes.try_dequeue(mailboxId))
				break;
			if (mailboxId == 0)
				continue;

			auto mailbox = FindMailbox(mailboxId);
			if (!mailbox)
				continue;

			++m_metrics.mailboxProcessCount;
			ProcessMailbox(mailbox, budgetPerMailbox);
		}
	}

	std::shared_ptr<Mailbox> ShardExecutor::FindMailbox(uint32 id)
	{
		if (id == 0)
			return nullptr;

		READ_LOCK
		auto it = m_mailboxes.find(id);
		return (it != m_mailboxes.end()) ? it->second : nullptr;
	}

	bool ShardExecutor::IsShardThread() const
	{
		return m_thread.joinable() && m_thread.get_id() == std::this_thread::get_id();
	}

	void ShardExecutor::WaitUntilLoopExited() const
	{
		std::unique_lock<std::mutex> lock(m_metricSyncMutex);
		m_metricSyncCv.wait(lock, [this]() { return m_loopExited; });
	}

	void ShardExecutor::ResetMetricsUnsafe()
	{
		m_metrics = {};
		m_metrics.shardIndex = m_config.index;

		if (m_scheduler)
			m_scheduler->ResetProfile();
	}

	ShardExecutorMetrics ShardExecutor::Profile() const
	{
		if (IsShardThread())
			return m_metrics;

		if (!m_running.load(std::memory_order_acquire))
		{
			if (m_thread.joinable())
				WaitUntilLoopExited();
			return m_metrics;
		}

		auto request = std::make_shared<ShardMetricsSnapshotRequest>();
		const_cast<ShardExecutor*>(this)->Submit(Job([this, request]()
			{
				const auto snapshot = m_metrics;
				{
					std::scoped_lock guard(m_metricSyncMutex);
					request->snapshot = snapshot;
					request->done = true;
				}
				m_metricSyncCv.notify_all();
			}, eJobPriority::Control));

		std::unique_lock<std::mutex> lock(m_metricSyncMutex);
		m_metricSyncCv.wait(lock, [this, &request]()
			{
				return request->done || m_loopExited;
			});

		if (request->done)
			return request->snapshot;

		return m_metrics;
	}

	void ShardExecutor::ResetMetrics()
	{
		if (IsShardThread())
		{
			ResetMetricsUnsafe();
			return;
		}

		if (!m_running.load(std::memory_order_acquire))
		{
			if (m_thread.joinable())
				WaitUntilLoopExited();
			ResetMetricsUnsafe();
			return;
		}

		auto request = std::make_shared<ShardMetricsResetRequest>();
		Submit(Job([this, request]()
			{
				ResetMetricsUnsafe();
				{
					std::scoped_lock guard(m_metricSyncMutex);
					request->done = true;
				}
				m_metricSyncCv.notify_all();
			}, eJobPriority::Control));

		std::unique_lock<std::mutex> lock(m_metricSyncMutex);
		m_metricSyncCv.wait(lock, [this, &request]()
			{
				return request->done || m_loopExited;
			});

		if (!request->done)
			ResetMetricsUnsafe();
	}
}
