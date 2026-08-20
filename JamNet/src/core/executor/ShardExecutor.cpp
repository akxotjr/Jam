#include "pch.h"

#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/core/executor/Job.h"
#include "jamnet/core/executor/ShardExecutor.h"
#include "jamnet/core/executor/GlobalExecutor.h"


namespace jam
{
	namespace
	{
		inline constexpr uint32 k_maxConsecutiveControlLocalPops = 10;
	}

	ShardExecutor::ShardExecutor(const ShardExecutorConfig& config)
			: m_config(config)
	{
		m_scheduler	= std::make_unique<FiberScheduler>(m_backend);
		m_local.shardIndex = static_cast<uint32>(m_config.index);
		m_local.networkMetrics.Init(m_local.shardIndex);
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
		m_workerRunning.store(true, std::memory_order_release);
		m_workerThread = std::thread([this]() { WorkerLoop(); });

		m_local.scheduler = m_scheduler.get();

		m_thread = std::thread([this]()
			{
				std::string threadName = std::format("ShardExecutor#{}", m_config.index);
				InitThreadContext(threadName, this);

				if (m_pinEnabled)
				{
					if (PinCurrentThreadTo(m_pinSlot))
					{
						JAM_LOG_DEBUG("ShardExecutor#{} pinned. group={}, mask=0x{:X}",
							m_config.index,
							m_pinSlot.group,
							static_cast<unsigned long long>(m_pinSlot.mask));
					}
					else
					{
						const DWORD errorCode = GetLastError();
						JAM_LOG_WARN_LOC("ShardExecutor#{} pinning failed. group={}, mask=0x{:X}, error={}",
							m_config.index,
							m_pinSlot.group,
							static_cast<unsigned long long>(m_pinSlot.mask),
							errorCode);
					}
				}

				try 
				{
					BindShardContext(&m_local, std::this_thread::get_id());
				}
				catch (const std::exception& ex) 
				{
					std::cerr << "[ShardExecutor] Bind failed: " << ex.what() << "\n";
					return;
				}

				m_scheduler->AttachToCurrentThread();
				Loop();
				m_scheduler->DetachFromThread();
				m_local.scheduler = nullptr;

				UnbindShardContext();
			});
	}

	void ShardExecutor::Stop()
	{
		if (!m_running.exchange(false))
			return;

		NotifyWorkAvailable();
		m_workerRunning.store(false, std::memory_order_release);
		m_workerWakeEpoch.fetch_add(1, std::memory_order_release);
		m_workerWakeEpoch.notify_all();

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
		if (m_workerThread.joinable())
			m_workerThread.join();
	}

	void ShardExecutor::Submit(Job j)
	{
		m_ingressJobSubmitCount.fetch_add(1, std::memory_order_relaxed);
		m_jobIngress.enqueue(std::move(j));
		NotifyWorkAvailable();
	}

	void ShardExecutor::SubmitWorkerJob(Job j)
	{
		m_workerJobSubmitCount.fetch_add(1, std::memory_order_relaxed);
		m_workerIngress.enqueue(std::move(j));
		m_workerWakeEpoch.fetch_add(1, std::memory_order_release);
		m_workerWakeEpoch.notify_one();
	}

	void ShardExecutor::WorkerLoop()
	{
		std::string threadName = std::format("ShardPhysics#{}", m_config.index);
		InitThreadContext(threadName, nullptr);

		if (m_pinEnabled && m_pinSlot.siblingMask != 0)
		{
			const CoreSlot siblingSlot{
				.group = m_pinSlot.group,
				.mask = m_pinSlot.siblingMask,
			};

			if (PinCurrentThreadTo(siblingSlot))
			{
				JAM_LOG_DEBUG("ShardPhysics#{} pinned. group={}, mask=0x{:X}",
					m_config.index, siblingSlot.group, static_cast<unsigned long long>(siblingSlot.mask));
			}
			else
			{
				JAM_LOG_WARN_LOC("ShardPhysics#{} pinning failed. group={}, mask=0x{:X}, error={}",
					m_config.index, siblingSlot.group, static_cast<unsigned long long>(siblingSlot.mask), GetLastError());
			}
		}
		else if (m_pinEnabled)
		{
			JAM_LOG_WARN("ShardPhysics#{} has no SMT sibling affinity; worker remains unpinned", m_config.index);
		}

		constexpr uint32 kActiveSpinCount = 256;
		constexpr uint64 kPreWakeSpinNs = 1_ms;
		for (;;)
		{
			Job job;
			if (m_workerIngress.try_dequeue(job))
			{
				job.Execute();
				continue;
			}

			if (!m_workerRunning.load(std::memory_order_acquire))
				break;

			const uint64 preWakeDeadlineNs = m_workerPreWakeDeadlineNs.exchange(0, std::memory_order_acq_rel);
			if (preWakeDeadlineNs != 0)
			{
				const uint64 spinStartNs = NOW_NS();
				const uint64 spinEndNs = spinStartNs + kPreWakeSpinNs;
				bool preWakeHit = false;
				while (NOW_NS() < spinEndNs)
				{
					if (m_workerIngress.try_dequeue(job))
					{
						preWakeHit = true;
						break;
					}
					_mm_pause();
				}

				if (preWakeHit)
				{
					job.Execute();
					continue;
				}
			}

			bool foundWork = false;
			for (uint32 spin = 0; spin < kActiveSpinCount; ++spin)
			{
				if (m_workerIngress.try_dequeue(job))
				{
					job.Execute();
					foundWork = true;
					break;
				}
				_mm_pause();
			}
			if (foundWork)
				continue;

			const uint64 observedWakeEpoch = m_workerWakeEpoch.load(std::memory_order_acquire);
			if (m_workerIngress.try_dequeue(job))
			{
				job.Execute();
				continue;
			}
			if (!m_workerRunning.load(std::memory_order_acquire))
				break;

			m_workerWakeEpoch.wait(observedWakeEpoch, std::memory_order_acquire);
		}
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
		NotifyWorkAvailable();
	}

	Mailbox* ShardExecutor::CreateMailbox()
	{
		auto id = m_nextMailboxId.fetch_add(1, std::memory_order_relaxed);
		auto mb = std::make_unique<Mailbox>(id, weak_from_this());
		auto* mbRaw = mb.get();
		{
			WRITE_LOCK
			m_mailboxes.emplace(id, std::move(mb));
		}

		if (m_shardSlot)
		{
			auto& qs = m_shardSlot->inbox;
			qs.state.store(E2U(eShardState::Closed), std::memory_order_release);
			qs.q.store(mbRaw, std::memory_order_release);
			qs.gen.fetch_add(1, std::memory_order_acq_rel);
			qs.state.store(E2U(eShardState::Open), std::memory_order_release);
		}

		return mbRaw;
	}

	MailboxRef ShardExecutor::CreateMailboxRef(RuntimeId ownerId)
	{
		if (ownerId == kInvalidRuntimeId)
			return {};

		Mailbox* mailbox = CreateMailbox();
		if (!mailbox)
			return {};

		return MailboxRef
		{
			.mailbox = mailbox,
			.ownerId = ownerId,
			.generation = GetRuntimeGeneration(ownerId),
		};
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

	bool ShardExecutor::NotifyReady(uint32 mailboxId)
	{
		if (mailboxId == 0)
			return false;

		if (!m_readyMailboxes.enqueue(ReadyMailboxEntry{ .mailboxId = mailboxId, .enqueuedAt_ns = NOW_NS() }))
			return false;
		NotifyWorkAvailable();
		return true;
	}

	void ShardExecutor::PinCoreSlot(const CoreSlot& slot, uint16 numaNode)
	{
		m_pinSlot = slot;
		m_pinEnabled = true;
		if (numaNode != 0xFFFF)
			m_config.numaNode = numaNode;
	}

	void ShardExecutor::SpawnFiber(FiberFn fn, const FiberDesc& desc)
	{
		m_scheduler->PostSpawn(std::move(fn), desc);
		NotifyWorkAvailable();
	}

	void ShardExecutor::ResumeFiber(FiberAwaitKey key)
	{
		m_scheduler->PostResume(key);
		NotifyWorkAvailable();
	}

	void ShardExecutor::ResumeFiber(FiberAwaitKey key, int32 readyPriority)
	{
		m_scheduler->PostResume(key, readyPriority);
		NotifyWorkAvailable();
	}

	void ShardExecutor::CancelFiberByKey(FiberAwaitKey key, eCancelCode code)
	{
		m_scheduler->PostCancelByKey(key, code);
		NotifyWorkAvailable();
	}

	void ShardExecutor::CancelFiberById(uint32 id, eCancelCode code)
	{
		m_scheduler->PostCancelById(id, code);
		NotifyWorkAvailable();
	}

	FiberAwaitKey ShardExecutor::AllocateAwaitKey()
	{
		return m_nextAwaitSeq.fetch_add(1, std::memory_order_relaxed);
	}

	bool ShardExecutor::RunDueDomainGroups(uint64 now_ns)
	{
		auto& L = m_local;
		bool didWork = false;
		
		for (auto& group : L.domainGroups | std::views::values)
		{
			if (!group.bootstraps.empty())
			{
				auto bootstraps = std::move(group.bootstraps);
				group.bootstraps.clear();

				for (auto& fn : bootstraps)
				{
					if (fn) fn(L);
				}

				didWork = true;
			}

			if (group.systems.empty())
				continue;

			const uint64 period_ns = group.tickPeriod_ns;

			if (period_ns == 0)
			{
				group.nextTick_ns = now_ns;

				for (auto& fn : group.systems)
				{
					if (fn)
						fn(L, now_ns, 0_ns);
				}

				++m_metrics.tickCount;
				didWork = true;
				continue;
			}

			if (group.nextTick_ns == 0)
				group.nextTick_ns = now_ns;

			if (now_ns < group.nextTick_ns)
				continue;

			int32 catches = 0;
			const int32 maxCatchUp = std::max(1, m_config.maxTickCatchUp);
			while (now_ns >= group.nextTick_ns && catches < maxCatchUp)
			{
				const uint64 tickNow_ns = group.nextTick_ns;
				group.nextTick_ns += period_ns;

				for (auto& fn : group.systems)
				{
					if (fn)
						fn(L, tickNow_ns, period_ns);
				}

				++m_metrics.tickCount;
				++catches;
				didWork = true;
			}

			if (now_ns >= group.nextTick_ns)
			{
				group.nextTick_ns = now_ns + period_ns;
				++m_metrics.tickCatchUpCount;
			}
		}

		return didWork;
	}

	void ShardExecutor::PublishDueWorkerPreWakes(uint64 now_ns)
	{
		uint64 spinDeadlineNs = 0;
		for (auto& group : m_local.domainGroups | std::views::values)
		{
			if (group.systems.empty() || group.tickPeriod_ns == 0 || group.nextTick_ns == 0
				|| group.workerPreWakeLead_ns == 0 || group.workerPreWakePublishedTick_ns == group.nextTick_ns)
				continue;

			const uint64 preWakeNs = group.nextTick_ns > group.workerPreWakeLead_ns
				? group.nextTick_ns - group.workerPreWakeLead_ns
				: 0;
			if (now_ns < preWakeNs)
				continue;

			group.workerPreWakePublishedTick_ns = group.nextTick_ns;
			if (spinDeadlineNs == 0 || group.nextTick_ns < spinDeadlineNs)
			{
				spinDeadlineNs = group.nextTick_ns;
			}
		}

		if (spinDeadlineNs == 0)
			return;

		m_workerPreWakeDeadlineNs.store(spinDeadlineNs, std::memory_order_release);
		m_workerWakeEpoch.fetch_add(1, std::memory_order_release);
		m_workerWakeEpoch.notify_one();
	}

	bool ShardExecutor::ProcessDefers()
	{
		auto& L = m_local;
		if (!L.defers.empty())
		{
			auto defers = std::move(L.defers);
			L.defers.clear();

			for (auto& f : defers)
			{
				if (f) f(L.registry);
			}

			return true;
		}

		return false;
	}

	uint64 ShardExecutor::TimeUntilNextDomainDue(uint64 now_ns) const
	{
		uint64 wait_ns = UINT64_MAX;

		for (const auto& group : m_local.domainGroups | std::views::values)
		{
			if (!group.bootstraps.empty())
				return 0_ns;

			if (group.systems.empty())
				continue;

			const uint64 period_ns = group.tickPeriod_ns;
			if (period_ns == 0 || group.nextTick_ns == 0 || now_ns >= group.nextTick_ns)
				return 0_ns;

			wait_ns = std::min(wait_ns, group.nextTick_ns - now_ns);
		}

		return wait_ns;
	}

	uint64 ShardExecutor::TimeUntilNextWorkerPreWake(uint64 now_ns) const
	{
		uint64 wait_ns = UINT64_MAX;
		for (const auto& group : m_local.domainGroups | std::views::values)
		{
			if (group.systems.empty() || group.tickPeriod_ns == 0 || group.nextTick_ns == 0
				|| group.workerPreWakeLead_ns == 0 || group.workerPreWakePublishedTick_ns == group.nextTick_ns)
				continue;

			const uint64 preWakeNs = group.nextTick_ns > group.workerPreWakeLead_ns
				? group.nextTick_ns - group.workerPreWakeLead_ns
				: 0;
			if (now_ns >= preWakeNs)
				return 0_ns;
			wait_ns = std::min(wait_ns, preWakeNs - now_ns);
		}
		return wait_ns;
	}



	void ShardExecutor::BeginDrain()
	{
		if (!m_shardSlot)
			return;

		m_shardSlot->inbox.state.store(E2U(eShardState::Draining), std::memory_order_release);
	}

	PeriodicHandle ShardExecutor::ScheduleFixedRate(Job j, const PeriodicOptions& opt)
	{
		if (opt.period_ns == 0)
			return {};

		auto st				= std::make_shared<PeriodicState>();
		st->period_ns		= opt.period_ns;
		st->initialDelay_ns = opt.initialDelay_ns;
		st->maxCatchUp		= opt.maxCatchUp;
		st->fixedRate		= true;
		st->awaitKey		= m_nextAwaitSeq.fetch_add(1, std::memory_order_relaxed);

		const uint32 id = m_periodicId.fetch_add(1, std::memory_order_relaxed);
		{
			WRITE_LOCK
			m_periodics[id] = st;
		}

		const char* fiberName = opt.name ? opt.name : "Shard.PeriodicFixedRate";
		auto sharedJob = std::make_shared<Job>(std::move(j));
		const eJobPriority priority = sharedJob->Priority();

		m_scheduler->PostSpawn(
			[this, st, sharedJob, priority, id]()
			{
				uint64 next_ns = NOW_NS() + st->initialDelay_ns;
				const uint64 period_ns = st->period_ns;

				while (m_running.load(std::memory_order_acquire) && !st->cancelled.load(std::memory_order_relaxed))
				{
					m_scheduler->Suspend(st->awaitKey, next_ns);
					if (!m_running.load(std::memory_order_acquire) || st->cancelled.load(std::memory_order_relaxed))
						break;

					Submit(Job([sharedJob]()
						{
							sharedJob->Execute();
						}, priority));

					next_ns += period_ns;

					int32 catches = 0;
					for (uint64 now_ns = NOW_NS(); now_ns >= next_ns && catches < st->maxCatchUp; now_ns = NOW_NS(), ++catches)
					{
						if (!m_running.load(std::memory_order_acquire) || st->cancelled.load(std::memory_order_relaxed))
							break;
						Submit(Job([sharedJob]()
							{
								sharedJob->Execute();
							}, priority));
						next_ns += period_ns;
					}
				}

				WRITE_LOCK
				m_periodics.erase(id);
			},
			FiberDesc{ .name = fiberName }
		);
		NotifyWorkAvailable();

		return PeriodicHandle{ id };
	}

	PeriodicHandle ShardExecutor::ScheduleFixedDelay(Job j, const PeriodicOptions& opt)
	{
		if (opt.period_ns == 0)
			return {};

		auto st = std::make_shared<PeriodicState>();
		st->period_ns		= opt.period_ns;
		st->initialDelay_ns = opt.initialDelay_ns;
		st->maxCatchUp		= 0;
		st->fixedRate		= false;
		st->awaitKey		= m_nextAwaitSeq.fetch_add(1, std::memory_order_relaxed);

		const uint32 id = m_periodicId.fetch_add(1, std::memory_order_relaxed);
		{
			WRITE_LOCK
			m_periodics[id] = st;
		}

		const char* fiberName = opt.name ? opt.name : "Shard.PeriodicFD";
		auto sharedJob = std::make_shared<Job>(std::move(j));
		const eJobPriority priority = sharedJob->Priority();

		m_scheduler->PostSpawn(
			[this, st, sharedJob, priority, id]()
			{
				uint64 next = NOW_NS() + st->initialDelay_ns;
				m_scheduler->Suspend(st->awaitKey, next);

				while (m_running.load(std::memory_order_acquire) && !st->cancelled.load(std::memory_order_relaxed))
				{
					Submit(Job([sharedJob]()
						{
							sharedJob->Execute();
						}, priority));
					if (!m_running.load(std::memory_order_acquire) || st->cancelled.load(std::memory_order_relaxed))
						break;

					m_scheduler->Suspend(st->awaitKey, NOW_NS() + st->period_ns);
				}

				WRITE_LOCK
				m_periodics.erase(id);
			},
			FiberDesc{ .name = fiberName }
		);
		NotifyWorkAvailable();

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
			m_scheduler->PostCancelByKey(st->awaitKey, eCancelCode::Manual);
			NotifyWorkAvailable();
			return true;
		}
		return false;
	}

	void ShardExecutor::Loop()
	{
		while (m_running.load())
		{
			const uint64 loopStart_ns = NOW_NS();
			UpdateMetricsWindow(loopStart_ns, 0_ns, 0, 0, 0);
			const uint64 ingressMovedBefore = m_metrics.ingressJobCount;
			const uint64 mailboxMovedBefore = m_metrics.mailboxJobMoveCount;
			const uint64 jobsExecutedBefore = m_metrics.processJobsExecCount;
			++m_metrics.loopCount;
			bool didWork = false;
			const uint64 observedWakeEpoch = m_wakeEpoch.load(std::memory_order_acquire);
			PublishDueWorkerPreWakes(NOW_NS());

			didWork |= ProcessJobsOnce(m_config.localExecuteBudgetPerPreTickPass);
			DrainReadyMailboxes(
				m_config.mailboxServiceBudgetPerLoop,
				m_config.mailboxMoveBudgetPerLoop,
				m_config.mailboxMoveBudgetPerMailbox);
			didWork |= ProcessJobsOnce(m_config.localExecuteBudgetPerPreTickPass);

			const uint64 pollStart_ns = NOW_NS();
			
			m_scheduler->Poll(m_config.schedulerPollBudgetPerLoop, pollStart_ns);

			const uint64 pollCost_ns = NOW_NS() - pollStart_ns;
			++m_metrics.schedulerPollCount;
			m_metrics.schedulerPollCost_ns += pollCost_ns;

			const auto& fiberMetrics = m_scheduler->Profile();
			if (fiberMetrics.lastPollCost_ns != 0 && fiberMetrics.lastPollReadyRunCount == 0)
				++m_metrics.schedulerEmptyPollCount;
			m_metrics.schedulerReadyRunCount = fiberMetrics.readyRunCount;

			if (fiberMetrics.lastPollReadyRunCount > 0)
				didWork = true;

			const uint64 now_ns = NOW_NS();
			PublishDueWorkerPreWakes(now_ns);
			didWork |= RunDueDomainGroups(now_ns);
			didWork |= ProcessDefers();

			// Domain work may submit asynchronous work whose completion resumes a
			// fiber from another thread. Drain those resumes before sleeping or
			// starting the next executor loop.
			const uint64 secondPollStart_ns = NOW_NS();
			m_scheduler->Poll(m_config.schedulerPollBudgetPerLoop, secondPollStart_ns);
			const uint64 secondPollCost_ns = NOW_NS() - secondPollStart_ns;
			++m_metrics.schedulerPollCount;
			m_metrics.schedulerPollCost_ns += secondPollCost_ns;
			const auto& secondFiberMetrics = m_scheduler->Profile();
			if (secondFiberMetrics.lastPollCost_ns != 0 && secondFiberMetrics.lastPollReadyRunCount == 0)
				++m_metrics.schedulerEmptyPollCount;
			m_metrics.schedulerReadyRunCount = secondFiberMetrics.readyRunCount;
			if (secondFiberMetrics.lastPollReadyRunCount > 0)
				didWork = true;

			// Preserve the pre-tick work bound while retaining additional local
			// recovery capacity after domain deadlines and fiber resumes are serviced.
			didWork |= ProcessJobsOnce(m_config.localRecoveryBudgetAfterTick, false);

			if (!didWork)
			{
				++m_metrics.idleLoopCount;
				const uint64 idleStart_ns = NOW_NS();
				const uint64 idleNow_ns = idleStart_ns;
				uint64 idleWait_ns = static_cast<uint64>(std::max(0, m_config.idleSleepMs)) * 1'000'000ull;
				const uint64 nextDomainDue_ns = TimeUntilNextDomainDue(idleNow_ns);
				if (nextDomainDue_ns != UINT64_MAX)
					idleWait_ns = std::min(idleWait_ns, nextDomainDue_ns);
				const uint64 nextWorkerPreWake_ns = TimeUntilNextWorkerPreWake(idleNow_ns);
				if (nextWorkerPreWake_ns != UINT64_MAX)
					idleWait_ns = std::min(idleWait_ns, nextWorkerPreWake_ns);
				const uint64 nextFiberWakeup_ns = m_scheduler->NextWakeupTime();
				if (nextFiberWakeup_ns != 0)
				{
					idleWait_ns = (idleNow_ns < nextFiberWakeup_ns)
						? std::min(idleWait_ns, nextFiberWakeup_ns - idleNow_ns)
						: 0_ns;
				}
				WaitForWorkOrTimeout(observedWakeEpoch, idleWait_ns);
				m_metrics.idleSleepCost_ns += NOW_NS() - idleStart_ns;
			}
			else
			{
				++m_metrics.didWorkLoopCount;
			}

			const uint64 loopEnd_ns = NOW_NS();
			UpdateMetricsWindow(
				loopEnd_ns,
				loopEnd_ns - loopStart_ns,
				m_metrics.ingressJobCount - ingressMovedBefore,
				m_metrics.mailboxJobMoveCount - mailboxMovedBefore,
				m_metrics.processJobsExecCount - jobsExecutedBefore);
		}
	}

	void ShardExecutor::NotifyWorkAvailable()
	{
		m_wakeEpoch.fetch_add(1, std::memory_order_release);
		m_wakeCv.notify_one();
	}

	void ShardExecutor::WaitForWorkOrTimeout(uint64 observedWakeEpoch, uint64 timeout_ns)
	{
		if (timeout_ns == 0)
		{
			std::this_thread::yield();
			return;
		}

		std::unique_lock lock(m_wakeMutex);
		m_wakeCv.wait_for(
			lock,
			std::chrono::nanoseconds(timeout_ns),
			[this, observedWakeEpoch]()
			{
				return !m_running.load(std::memory_order_acquire)
					|| m_wakeEpoch.load(std::memory_order_acquire) != observedWakeEpoch;
			});
	}


	void ShardExecutor::PushLocal(Job&& j)
	{
		if (j.Priority() == eJobPriority::Critical)
			m_jobLocalCritical.push_back(std::move(j));
		else if (j.Priority() == eJobPriority::Control)
			m_jobLocalControl.push_back(std::move(j));
		else
			m_jobLocalBackground.push_back(std::move(j));

		m_jobLocalTotal.fetch_add(1, std::memory_order_relaxed);
	}

	bool ShardExecutor::TryPopLocal(Job& j)
	{
		if (m_jobLocalTotal.load(std::memory_order_relaxed) == 0)
			return false;

		if (!m_jobLocalCritical.empty())
		{
			j = std::move(m_jobLocalCritical.front());
			m_jobLocalCritical.pop_front();
			m_consecutiveControlPops = 0;
			m_jobLocalTotal.fetch_sub(1, std::memory_order_relaxed);
			return true;
		}

		const bool hasControl = !m_jobLocalControl.empty();
		const bool hasBackground = !m_jobLocalBackground.empty();

		if (hasBackground && (!hasControl || m_consecutiveControlPops >= k_maxConsecutiveControlLocalPops))
		{
			j = std::move(m_jobLocalBackground.front());
			m_jobLocalBackground.pop_front();
			m_consecutiveControlPops = 0;
			m_jobLocalTotal.fetch_sub(1, std::memory_order_relaxed);
			return true;
		}

		if (hasControl)
		{
			j = std::move(m_jobLocalControl.front());
			m_jobLocalControl.pop_front();
			++m_consecutiveControlPops;
			m_jobLocalTotal.fetch_sub(1, std::memory_order_relaxed);
			return true;
		}

		if (hasBackground)
		{
			j = std::move(m_jobLocalBackground.front());
			m_jobLocalBackground.pop_front();
			m_consecutiveControlPops = 0;
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

		//size_t remained = m_jobIngress.size_approx();
		//if (m_config.index == 1 && remained != 0)
		//	JAMNET_LOG_DEBUG("[ShardExecutor::DrainIngressOnce] drain count= {}, remain ingress queue size(approx)= {}", n, remained);

		++m_metrics.ingressBatchCount;
		m_metrics.ingressJobCount += static_cast<uint64>(n);

		for (size_t i = 0; i < n; ++i)
		{
			PushLocal(std::move(batch[i]));
		}

		return true;
	}

	bool ShardExecutor::ProcessJobsOnce(int32 executeBudget, bool drainIngress)
	{
		++m_metrics.processJobsCallCount;
		bool didWork = false;
		uint64 execCount = 0;

		if (drainIngress)
		{
			// 1) 외부 Submit()가 넣은 ingress를 먼저 로컬로 땡김
			didWork |= DrainIngressOnce(m_config.ingressMoveBudgetPerLoop);

			// 2) 로컬 큐가 비면 한 번 더 기회(타이밍 경합 완화)
			if (m_jobLocalTotal.load(std::memory_order_relaxed) == 0)
				didWork |= DrainIngressOnce(m_config.ingressMoveBudgetPerLoop);
		}

		// 3) 실행
		for (int32 i = 0; i < executeBudget; ++i)
		{
			Job j;
			if (!TryPopLocal(j))
				break;

			const eJobPriority priority = j.Priority();
			j.Execute();
			didWork = true;
			++execCount;
			if (priority == eJobPriority::Critical)
				++m_metrics.jobsExecutedCritical;
			else if (priority == eJobPriority::Control)
				++m_metrics.jobsExecutedControl;
			else
				++m_metrics.jobsExecutedBackground;
		}

		m_metrics.processJobsExecCount += execCount;

		return didWork;
	}

	uint64 ShardExecutor::ProcessMailbox(Mailbox* mb, int32 budget)
	{
		if (!mb || budget <= 0) return 0;

		thread_local std::vector<Job> batch;

		batch.clear();
		batch.resize(static_cast<size_t>(budget));
		uint64 movedCount = 0;

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
				movedCount = n;
				if (n == static_cast<uint64>(budget) && !mb->IsEmpty())
					++m_metrics.mailboxPerMailboxBudgetHitCount;
			}
		}

		mb->EndConsume();

		if (mb->TryFinalizeClose())
		{
			RemoveMailbox(mb->GetId());
			return movedCount;
		}

		if (!mb->IsEmpty() && mb->TryBeginConsume())
		{
			if (!NotifyReady(mb->GetId()))
				mb->EndConsume();
		}

		return movedCount;
	}


	void ShardExecutor::DrainReadyMailboxes(int32 maxMailboxes, int32 totalJobBudget, int32 budgetPerMailbox)
	{
		if (maxMailboxes <= 0 || totalJobBudget <= 0 || budgetPerMailbox <= 0)
			return;

		ReadyMailboxEntry readyEntry = {};
		int32 remainingJobBudget = totalJobBudget;
		int32 servicedMailboxes = 0;

		for (; servicedMailboxes < maxMailboxes && remainingJobBudget > 0; ++servicedMailboxes)
		{
			if (!m_readyMailboxes.try_dequeue(readyEntry))
				break;
			const uint32 mailboxId = readyEntry.mailboxId;
			if (mailboxId == 0)
				continue;

			auto mailbox = FindMailbox(mailboxId);
			if (!mailbox)
				continue;

			const uint64 now_ns = NOW_NS();
			const uint64 readyWait_ns = readyEntry.enqueuedAt_ns != 0 && now_ns >= readyEntry.enqueuedAt_ns
				? now_ns - readyEntry.enqueuedAt_ns
				: 0;
			++m_metrics.mailboxReadyWaitSampleCount;
			m_metrics.mailboxReadyWaitTotal_ns += readyWait_ns;
			m_metrics.mailboxReadyWaitMax_ns = std::max(m_metrics.mailboxReadyWaitMax_ns, readyWait_ns);

			++m_metrics.mailboxProcessCount;
			const int32 mailboxBudget = std::min(budgetPerMailbox, remainingJobBudget);
			const uint64 moved = ProcessMailbox(mailbox, mailboxBudget);
			remainingJobBudget -= static_cast<int32>(std::min<uint64>(moved, static_cast<uint64>(remainingJobBudget)));
		}

		const bool hasReadyBacklog = m_readyMailboxes.size_approx() != 0;
		const bool totalJobBudgetHit = remainingJobBudget == 0 && hasReadyBacklog;
		const bool mailboxCountBudgetHit = servicedMailboxes == maxMailboxes && hasReadyBacklog;
		m_metrics.mailboxTotalJobBudgetHitCount += totalJobBudgetHit ? 1 : 0;
		m_metrics.mailboxCountBudgetHitCount += mailboxCountBudgetHit ? 1 : 0;
		m_metrics.mailboxServiceBudgetExhaustedLoopCount += totalJobBudgetHit || mailboxCountBudgetHit ? 1 : 0;
	}

	Mailbox* ShardExecutor::FindMailbox(uint32 id)
	{
		if (id == 0)
			return nullptr;

		READ_LOCK
		auto it = m_mailboxes.find(id);
		return (it != m_mailboxes.end()) ? it->second.get() : nullptr;
	}

	void ShardExecutor::ResetMetricsWindow()
	{
		m_metrics = {};
		m_metrics.shardIndex = m_config.index;
		if (m_scheduler)
			m_scheduler->ResetProfile();
	}

	void ShardExecutor::SubmitMetricsWindow(const uint64 windowEnd_ns)
	{
		m_metrics.ingressJobSubmitCount = m_ingressJobSubmitCount.exchange(0, std::memory_order_relaxed);
		m_metrics.workerJobSubmitCount = m_workerJobSubmitCount.exchange(0, std::memory_order_relaxed);
		MetricSnapshot snapshot{
			.windowIndex = m_metricsWindowIndex,
			.windowStartNs = m_metricsWindowStart_ns,
			.windowEndNs = windowEnd_ns,
			.scope = "executor_shard",
			.shardIndex = static_cast<uint32>(m_config.index),
			.values = {
				{ "loop_count", m_metrics.loopCount },
				{ "work_loop_count", m_metrics.didWorkLoopCount },
				{ "idle_loop_count", m_metrics.idleLoopCount },
				{ "idle_wait_ns", m_metrics.idleSleepCost_ns },
				{ "loop_duration_ns", m_metrics.loopDurationTotal_ns },
				{ "loop_duration_max_ns", m_metrics.loopDurationMax_ns, eMetricAggregation::Maximum },
				{ "ingress_batch_count", m_metrics.ingressBatchCount },
				{ "ingress_jobs_submitted", m_metrics.ingressJobSubmitCount },
				{ "ingress_jobs_moved", m_metrics.ingressJobCount },
				{ "worker_jobs_submitted", m_metrics.workerJobSubmitCount },
				{ "mailbox_service_count", m_metrics.mailboxProcessCount },
				{ "mailbox_jobs_moved", m_metrics.mailboxJobMoveCount },
				{ "mailbox_service_budget_exhausted_loop_count", m_metrics.mailboxServiceBudgetExhaustedLoopCount },
				{ "mailbox_total_job_budget_hit_count", m_metrics.mailboxTotalJobBudgetHitCount },
				{ "mailbox_count_budget_hit_count", m_metrics.mailboxCountBudgetHitCount },
				{ "mailbox_per_mailbox_budget_hit_count", m_metrics.mailboxPerMailboxBudgetHitCount },
				{ "mailbox_ready_wait_sample_count", m_metrics.mailboxReadyWaitSampleCount },
				{ "mailbox_ready_wait_ns", m_metrics.mailboxReadyWaitTotal_ns },
				{ "mailbox_ready_wait_max_ns", m_metrics.mailboxReadyWaitMax_ns, eMetricAggregation::Maximum },
				{ "jobs_executed", m_metrics.processJobsExecCount },
				{ "process_jobs_call_count", m_metrics.processJobsCallCount },
				{ "jobs_executed_critical", m_metrics.jobsExecutedCritical },
				{ "jobs_executed_control", m_metrics.jobsExecutedControl },
				{ "jobs_executed_background", m_metrics.jobsExecutedBackground },
				{ "scheduler_poll_count", m_metrics.schedulerPollCount },
				{ "scheduler_empty_poll_count", m_metrics.schedulerEmptyPollCount },
				{ "scheduler_poll_ns", m_metrics.schedulerPollCost_ns },
				{ "scheduler_ready_runs", m_metrics.schedulerReadyRunCount },
				{ "tick_count", m_metrics.tickCount },
				{ "tick_catch_up_count", m_metrics.tickCatchUpCount },
				{ "ingress_queue_current", m_metrics.ingressQueueCurrent, eMetricAggregation::Latest },
				{ "ingress_queue_peak", m_metrics.ingressQueuePeak, eMetricAggregation::Maximum },
				{ "worker_queue_current", m_metrics.workerQueueCurrent, eMetricAggregation::Latest },
				{ "worker_queue_peak", m_metrics.workerQueuePeak, eMetricAggregation::Maximum },
				{ "ready_mailbox_current", m_metrics.readyMailboxCurrent, eMetricAggregation::Latest },
				{ "ready_mailbox_peak", m_metrics.readyMailboxPeak, eMetricAggregation::Maximum },
				{ "ready_mailbox_sample_count", m_metrics.readyMailboxSampleCount },
				{ "ready_mailbox_sample_sum", m_metrics.readyMailboxSampleSum },
				{ "local_critical_current", m_metrics.localCriticalCurrent, eMetricAggregation::Latest },
				{ "local_critical_peak", m_metrics.localCriticalPeak, eMetricAggregation::Maximum },
				{ "local_control_current", m_metrics.localControlCurrent, eMetricAggregation::Latest },
				{ "local_control_peak", m_metrics.localControlPeak, eMetricAggregation::Maximum },
				{ "local_background_current", m_metrics.localBackgroundCurrent, eMetricAggregation::Latest },
				{ "local_background_peak", m_metrics.localBackgroundPeak, eMetricAggregation::Maximum },
				{ "local_total_current", m_metrics.localTotalCurrent, eMetricAggregation::Latest },
				{ "local_total_peak", m_metrics.localTotalPeak, eMetricAggregation::Maximum },
				{ "ingress_moved_per_loop_max", m_metrics.ingressMovedPerLoopMax, eMetricAggregation::Maximum },
				{ "mailbox_moved_per_loop_max", m_metrics.mailboxMovedPerLoopMax, eMetricAggregation::Maximum },
				{ "jobs_executed_per_loop_max", m_metrics.jobsExecutedPerLoopMax, eMetricAggregation::Maximum },
			}
		};
		GLOBAL_EXEC.SubmitMetrics(std::move(snapshot));
	}

	void ShardExecutor::UpdateMetricsWindow(
		const uint64 now_ns,
		const uint64 loopDuration_ns,
		const uint64 ingressMoved,
		const uint64 mailboxMoved,
		const uint64 jobsExecuted)
	{
		auto* aggregator = GLOBAL_EXEC.GetMetricsAggregator();
		if (!aggregator || !aggregator->IsEnabled())
			return;

		const uint64 windowIndex = aggregator->WindowIndex(now_ns);
		if (m_metricsWindowIndex == UINT64_MAX)
		{
			m_metricsWindowIndex = windowIndex;
			m_metricsWindowStart_ns = windowIndex * aggregator->WindowPeriodNs();
		}
		else if (windowIndex != m_metricsWindowIndex)
		{
			SubmitMetricsWindow(m_metricsWindowStart_ns + aggregator->WindowPeriodNs());
			ResetMetricsWindow();
			m_metricsWindowIndex = windowIndex;
			m_metricsWindowStart_ns = windowIndex * aggregator->WindowPeriodNs();
		}

		m_metrics.loopDurationTotal_ns += loopDuration_ns;
		m_metrics.loopDurationMax_ns = std::max(m_metrics.loopDurationMax_ns, loopDuration_ns);
		m_metrics.ingressMovedPerLoopMax = std::max(m_metrics.ingressMovedPerLoopMax, ingressMoved);
		m_metrics.mailboxMovedPerLoopMax = std::max(m_metrics.mailboxMovedPerLoopMax, mailboxMoved);
		m_metrics.jobsExecutedPerLoopMax = std::max(m_metrics.jobsExecutedPerLoopMax, jobsExecuted);

		auto updateGauge = [](uint64 current, uint64& gauge, uint64& peak)
			{
				gauge = current;
				peak = std::max(peak, current);
			};
		updateGauge(m_jobIngress.size_approx(), m_metrics.ingressQueueCurrent, m_metrics.ingressQueuePeak);
		updateGauge(m_workerIngress.size_approx(), m_metrics.workerQueueCurrent, m_metrics.workerQueuePeak);
		const uint64 readyMailboxCount = m_readyMailboxes.size_approx();
		updateGauge(readyMailboxCount, m_metrics.readyMailboxCurrent, m_metrics.readyMailboxPeak);
		++m_metrics.readyMailboxSampleCount;
		m_metrics.readyMailboxSampleSum += readyMailboxCount;
		updateGauge(m_jobLocalCritical.size(), m_metrics.localCriticalCurrent, m_metrics.localCriticalPeak);
		updateGauge(m_jobLocalControl.size(), m_metrics.localControlCurrent, m_metrics.localControlPeak);
		updateGauge(m_jobLocalBackground.size(), m_metrics.localBackgroundCurrent, m_metrics.localBackgroundPeak);
		updateGauge(m_jobLocalTotal.load(std::memory_order_relaxed), m_metrics.localTotalCurrent, m_metrics.localTotalPeak);
	}
}
