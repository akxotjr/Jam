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
		m_ingressJobSubmitCount.fetch_add(1, std::memory_order_relaxed);
		m_jobIngress.enqueue(std::move(j));
		NotifyWorkAvailable();
	}

	void ShardExecutor:: SubmitAfter(Job j, Duration delay)
	{
		if (!m_scheduler) 
		{
			Submit(std::move(j));
			return;
		}

		const Timestamp deadline = NOW() + delay;

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

		if (!m_readyMailboxes.enqueue(ReadyMailboxEntry{ .mailboxId = mailboxId, .enqueuedAt = NOW() }))
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

	bool ShardExecutor::RunDueDomainGroups(Timestamp now)
	{
		auto& L = m_local;
		bool didWork = false;

		// Domain callbacks may stop worlds or register new domains. Do not retain
		// map iterators or execute functions owned by a group across a callback.
		thread_local std::vector<ShardDomain> domains;
		thread_local std::vector<ShardSystemGroup::SystemFn> systems;
		domains.clear();
		for (const auto& [tag, group] : L.domainGroups)
			domains.push_back(tag);

		for (const auto tag : domains)
		{
			auto it = L.domainGroups.find(tag);
			if (it == L.domainGroups.end())
				continue;

			if (!it->second.bootstraps.empty())
			{
				auto bootstraps = std::move(it->second.bootstraps);
				it->second.bootstraps.clear();
				for (auto& fn : bootstraps)
					if (fn) fn(L);
				didWork = true;
			}

			int32 catches = 0;
			const int32 maxCatchUp = std::max(1, m_config.maxTickCatchUp);
			while (catches < maxCatchUp)
			{
				it = L.domainGroups.find(tag);
				if (it == L.domainGroups.end() || it->second.systems.empty())
					break;

				auto& group = it->second;
				const Duration period = group.tickPeriod;
				if (group.nextTick.IsZero() || period.IsZero())
					group.nextTick = now;
				if (now < group.nextTick)
					break;

				const Timestamp tickNow = group.nextTick;
				group.nextTick += period;
				systems.assign(group.systems.begin(), group.systems.end());
				for (const auto& fn : systems)
				{
					if (!L.domainGroups.contains(tag))
						break;
					if (fn) fn(L, tickNow, period);
				}
				systems.clear();
				++m_metrics.tickCount;
				++catches;
				didWork = true;
				didWork |= ServiceMailboxesAtBoundary();
				if (period.IsZero())
					break;
			}

			it = L.domainGroups.find(tag);
			if (catches == maxCatchUp && it != L.domainGroups.end()
				&& !it->second.tickPeriod.IsZero() && now >= it->second.nextTick)
			{
				it->second.nextTick = now + it->second.tickPeriod;
				++m_metrics.tickCatchUpCount;
			}
		}
		return didWork;
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

	Duration ShardExecutor::TimeUntilNextDomainDue(Timestamp now) const
	{
		Duration wait = Duration::Max();

		for (const auto& group : m_local.domainGroups | std::views::values)
		{
			if (!group.bootstraps.empty())
				return Duration::Zero();

			if (group.systems.empty())
				continue;

			if (group.tickPeriod.IsZero() || group.nextTick.IsZero() || now >= group.nextTick)
				return Duration::Zero();

			wait = std::min(wait, group.nextTick - now);
		}

		return wait;
	}

	void ShardExecutor::BeginDrain()
	{
		if (!m_shardSlot)
			return;

		m_shardSlot->inbox.state.store(E2U(eShardState::Draining), std::memory_order_release);
	}

	PeriodicHandle ShardExecutor::ScheduleFixedRate(Job j, const PeriodicOptions& opt)
	{
		if (opt.period.IsZero())
			return {};

		auto st				= std::make_shared<PeriodicState>();
		st->period			= opt.period;
		st->initialDelay	= opt.initialDelay;
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
				Timestamp next = NOW() + st->initialDelay;
				const Duration period = st->period;

				while (m_running.load(std::memory_order_acquire) && !st->cancelled.load(std::memory_order_relaxed))
				{
					m_scheduler->Suspend(st->awaitKey, next);
					if (!m_running.load(std::memory_order_acquire) || st->cancelled.load(std::memory_order_relaxed))
						break;

					Submit(Job([sharedJob]()
						{
							sharedJob->Execute();
						}, priority));

					next += period;

					int32 catches = 0;
					for (Timestamp now_ns = NOW(); now_ns >= next && catches < st->maxCatchUp; now_ns = NOW(), ++catches)
					{
						if (!m_running.load(std::memory_order_acquire) || st->cancelled.load(std::memory_order_relaxed))
							break;
						Submit(Job([sharedJob]()
							{
								sharedJob->Execute();
							}, priority));
						next += period;
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
		if (opt.period.IsZero())
			return {};

		auto st = std::make_shared<PeriodicState>();
		st->period		= opt.period;
		st->initialDelay = opt.initialDelay;
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
				Timestamp next = NOW() + st->initialDelay;
				m_scheduler->Suspend(st->awaitKey, next);

				while (m_running.load(std::memory_order_acquire) && !st->cancelled.load(std::memory_order_relaxed))
				{
					Submit(Job([sharedJob]()
						{
							sharedJob->Execute();
						}, priority));
					if (!m_running.load(std::memory_order_acquire) || st->cancelled.load(std::memory_order_relaxed))
						break;

					m_scheduler->Suspend(st->awaitKey, NOW() + st->period);
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
			const Timestamp loopStart = NOW();
			UpdateMetricsWindow(loopStart, 0_ns, 0, 0, 0);

			const uint64 ingressMovedBefore = m_metrics.ingressJobCount;
			const uint64 mailboxMovedBefore = m_metrics.mailboxJobMoveCount;
			const uint64 jobsExecutedBefore = m_metrics.processJobsExecCount;
			
			m_mailboxBudgetExhaustedThisLoop = false;
			++m_metrics.loopCount;
			bool didWork = false;

			const uint64 observedWakeEpoch = m_wakeEpoch.load(std::memory_order_acquire);
			

			didWork |= ProcessJobsOnce(m_config.localExecuteBudgetPerPreTickPass);
			DrainReadyMailboxes(m_config.mailboxServiceBudgetPerLoop, m_config.mailboxMoveBudgetPerLoop, m_config.mailboxMoveBudgetPerMailbox);
			didWork |= ProcessJobsOnce(m_config.localExecuteBudgetPerPreTickPass);

			const Timestamp pollStart_ns = NOW();
			m_scheduler->Poll(m_config.schedulerPollBudgetPerLoop, pollStart_ns);
			const Duration pollCost_ns = NOW() - pollStart_ns;

			++m_metrics.schedulerPollCount;
			m_metrics.schedulerPollCost_ns += pollCost_ns.Ns();

			const auto& fiberMetrics = m_scheduler->Profile();
			if (fiberMetrics.lastPollCost_ns != 0 && fiberMetrics.lastPollReadyRunCount == 0)
				++m_metrics.schedulerEmptyPollCount;
			
			m_metrics.schedulerReadyRunCount = fiberMetrics.readyRunCount;

			if (fiberMetrics.lastPollReadyRunCount > 0)
				didWork = true;

			didWork |= ServiceMailboxesAtBoundary();
			const Timestamp now_ns = NOW();
			didWork |= RunDueDomainGroups(now_ns);
			didWork |= ProcessDefers();

			// Domain work may submit asynchronous work whose completion resumes a
			// fiber from another thread. Drain those resumes before sleeping or
			// starting the next executor loop.
			const Timestamp secondPollStart_ns = NOW();
			m_scheduler->Poll(m_config.schedulerPollBudgetPerLoop, secondPollStart_ns);
			const Duration secondPollCost_ns = NOW() - secondPollStart_ns;

			++m_metrics.schedulerPollCount;
			m_metrics.schedulerPollCost_ns += secondPollCost_ns.Ns();

			const auto& secondFiberMetrics = m_scheduler->Profile();
			if (secondFiberMetrics.lastPollCost_ns != 0 && secondFiberMetrics.lastPollReadyRunCount == 0)
				++m_metrics.schedulerEmptyPollCount;

			m_metrics.schedulerReadyRunCount = secondFiberMetrics.readyRunCount;
			
			if (secondFiberMetrics.lastPollReadyRunCount > 0)
				didWork = true;

			// Service mailbox arrivals during defers/Poll before local recovery.
			didWork |= ServiceMailboxesAtBoundary();
			didWork |= ProcessJobsOnce(m_config.localRecoveryBudgetAfterTick, false);

			if (!didWork)
			{
				++m_metrics.idleLoopCount;
				const Timestamp idleStart = NOW();
				const Timestamp idleNow   = idleStart;

				Duration idleWait = m_config.idleSleepMs;
				
				const Duration nextDomainDue = TimeUntilNextDomainDue(idleNow);
				if (nextDomainDue != Duration::Max())
					idleWait = std::min(idleWait, nextDomainDue);
				

				const Timestamp nextFiberWakeup = m_scheduler->NextWakeupTime();
				if (!nextFiberWakeup.IsZero())
					idleWait = (idleNow < nextFiberWakeup) ? std::min(idleWait, nextFiberWakeup - idleNow) : Duration::Zero();
				
				WaitForWorkOrTimeout(observedWakeEpoch, idleWait);
				m_metrics.idleSleepCost_ns += (NOW() - idleStart).Ns();
			}
			else
			{
				++m_metrics.didWorkLoopCount;
			}

			m_metrics.mailboxServiceBudgetExhaustedLoopCount += m_mailboxBudgetExhaustedThisLoop ? 1 : 0;
			const Timestamp loopEnd = NOW();

			UpdateMetricsWindow(
				loopEnd,
				loopEnd - loopStart,
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

	void ShardExecutor::WaitForWorkOrTimeout(uint64 observedWakeEpoch, Duration timeout)
	{
		if (timeout.IsZero())
		{
			std::this_thread::yield();
			return;
		}

		std::unique_lock lock(m_wakeMutex);
		m_wakeCv.wait_for(
			lock,
			std::chrono::nanoseconds(timeout.Ns()),
			[this, observedWakeEpoch]()
			{
				return !m_running.load(std::memory_order_acquire) || m_wakeEpoch.load(std::memory_order_acquire) != observedWakeEpoch;
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

		++m_metrics.ingressBatchCount;
		m_metrics.ingressJobCount += static_cast<uint64>(n);

		for (size_t i = 0; i < n; ++i)
		{
			PushLocal(std::move(batch[i]));
		}

		return true;
	}

	bool ShardExecutor::ProcessJobsOnce(int32 executeBudget, bool drainIngress, Timestamp deadline)
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
			if (!deadline.IsZero() && NOW() >= deadline)
				break;
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


	void ShardExecutor::DrainReadyMailboxes(int32 maxMailboxes, int32 totalJobBudget, int32 budgetPerMailbox, Timestamp deadline)
	{
		if (maxMailboxes <= 0 || totalJobBudget <= 0 || budgetPerMailbox <= 0)
			return;

		ReadyMailboxEntry readyEntry = {};
		int32 remainingJobBudget = totalJobBudget;
		int32 servicedMailboxes = 0;

		for (; servicedMailboxes < maxMailboxes && remainingJobBudget > 0; ++servicedMailboxes)
		{
			if (!deadline.IsZero() && NOW() >= deadline)
				break;
			if (!m_readyMailboxes.try_dequeue(readyEntry))
				break;
			const uint32 mailboxId = readyEntry.mailboxId;
			if (mailboxId == 0)
				continue;

			auto mailbox = FindMailbox(mailboxId);
			if (!mailbox)
				continue;

			const Timestamp now = NOW();
			const Duration readyWait = !readyEntry.enqueuedAt.IsZero() && now >= readyEntry.enqueuedAt ? now - readyEntry.enqueuedAt : Duration::Zero();

			++m_metrics.mailboxReadyWaitSampleCount;
			m_metrics.mailboxReadyWaitTotal_ns += readyWait.Ns();
			m_metrics.mailboxReadyWaitMax_ns = std::max(m_metrics.mailboxReadyWaitMax_ns, readyWait.Ns());

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
		m_mailboxBudgetExhaustedThisLoop |= totalJobBudgetHit || mailboxCountBudgetHit;
	}

	bool ShardExecutor::ServiceMailboxesAtBoundary()
	{
		const Timestamp start = NOW();
		const Timestamp deadline = m_config.mailboxServiceTimeBudgetPerBoundary.IsZero()
			? Timestamp::Zero() : start + m_config.mailboxServiceTimeBudgetPerBoundary;
		const uint64 servicedBefore = m_metrics.mailboxProcessCount;
		const uint64 executedBefore = m_metrics.processJobsExecCount;

		// Move no more than this pass can execute. Existing local work retains
		// its normal priority; mailbox jobs are never executed out of band.
		DrainReadyMailboxes(m_config.mailboxServiceBudgetPerBoundary,
			std::min(m_config.mailboxMoveBudgetPerBoundary, m_config.localExecuteBudgetPerBoundary),
			m_config.mailboxMoveBudgetPerBoundaryMailbox, deadline);
		ProcessJobsOnce(m_config.localExecuteBudgetPerBoundary, false, deadline);
		++m_metrics.mailboxBoundaryServiceCount;
		if (!deadline.IsZero() && NOW() >= deadline
			&& (m_readyMailboxes.size_approx() != 0 || m_jobLocalTotal.load(std::memory_order_relaxed) != 0))
			++m_metrics.mailboxBoundaryTimeBudgetHitCount;
		return m_metrics.mailboxProcessCount != servicedBefore || m_metrics.processJobsExecCount != executedBefore;
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

	void ShardExecutor::SubmitMetricsWindow(const Timestamp windowEnd)
	{
		m_metrics.ingressJobSubmitCount = m_ingressJobSubmitCount.exchange(0, std::memory_order_relaxed);
		MetricSnapshot snapshot{
			.windowIndex = m_metricsWindowIndex,
			.windowStartNs = m_metricsWindowStart_ns.Ns(),
			.windowEndNs = windowEnd.Ns(),
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
				{ "mailbox_service_count", m_metrics.mailboxProcessCount },
				{ "mailbox_boundary_service_count", m_metrics.mailboxBoundaryServiceCount },
				{ "mailbox_boundary_time_budget_hit_count", m_metrics.mailboxBoundaryTimeBudgetHitCount },
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
		const Timestamp now,
		const Duration loopDuration,
		const uint64 ingressMoved,
		const uint64 mailboxMoved,
		const uint64 jobsExecuted)
	{
		auto* aggregator = GLOBAL_EXEC.GetMetricsAggregator();
		if (!aggregator || !aggregator->IsEnabled())
			return;

		const uint64 windowIndex = aggregator->WindowIndex(now);
		if (m_metricsWindowIndex == UINT64_MAX)
		{
			m_metricsWindowIndex    = windowIndex;
			m_metricsWindowStart_ns = Timestamp::FromNs(windowIndex * aggregator->WindowPeriod().Ns());
		}
		else if (windowIndex != m_metricsWindowIndex)
		{
			SubmitMetricsWindow(m_metricsWindowStart_ns + aggregator->WindowPeriod());
			ResetMetricsWindow();
			m_metricsWindowIndex    = windowIndex;
			m_metricsWindowStart_ns = Timestamp::FromNs(windowIndex * aggregator->WindowPeriod().Ns());
		}

		m_metrics.loopDurationTotal_ns	 += loopDuration.Ns();
		m_metrics.loopDurationMax_ns	 = std::max(m_metrics.loopDurationMax_ns, loopDuration.Ns());
		m_metrics.ingressMovedPerLoopMax = std::max(m_metrics.ingressMovedPerLoopMax, ingressMoved);
		m_metrics.mailboxMovedPerLoopMax = std::max(m_metrics.mailboxMovedPerLoopMax, mailboxMoved);
		m_metrics.jobsExecutedPerLoopMax = std::max(m_metrics.jobsExecutedPerLoopMax, jobsExecuted);

		auto updateGauge = [](uint64 current, uint64& gauge, uint64& peak)
			{
				gauge = current;
				peak = std::max(peak, current);
			};
		updateGauge(m_jobIngress.size_approx(), m_metrics.ingressQueueCurrent, m_metrics.ingressQueuePeak);
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
