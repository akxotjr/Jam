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
		m_local.shardIndex = static_cast<uint32>(m_config.index);
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
				InitThreadContext(threadName, this);

				if (m_pinEnabled)
				{
					if (PinCurrentThreadTo(m_pinSlot))
					{
						JAMNET_LOG_DEBUG("ShardExecutor#{} pinned. group={}, mask=0x{:X}",
							m_config.index,
							m_pinSlot.group,
							static_cast<unsigned long long>(m_pinSlot.mask));
					}
					else
					{
						const DWORD errorCode = GetLastError();
						JAMNET_LOG_WARN_LOC("ShardExecutor#{} pinning failed. group={}, mask=0x{:X}, error={}",
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
					markLoopExited();
					return;
				}

				m_scheduler->AttachToCurrentThread();
				Loop();
				m_scheduler->DetachFromThread();
				m_local.scheduler = nullptr;

				UnbindShardContext();
				markLoopExited();
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
		m_jobIngress.enqueue(std::move(j));
		NotifyWorkAvailable();
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

		if (!m_readyMailboxes.enqueue(mailboxId))
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
			++m_metrics.loopCount;
			bool didWork = false;
			const uint64 observedWakeEpoch = m_wakeEpoch.load(std::memory_order_acquire);

			didWork |= ProcessJobsOnce();
			DrainReadyMailboxes(64, 64 * m_config.batchBudget, m_config.batchBudget);
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

			const uint64 now_ns = NOW_NS();
			didWork |= RunDueDomainGroups(now_ns);
			didWork |= ProcessDefers();

			if (!didWork)
			{
				++m_metrics.idleLoopCount;
				const uint64 idleStart_ns = NOW_NS();
				uint64 idleWait_ns = static_cast<uint64>(std::max(0, m_config.idleSleepMs)) * 1'000'000ull;
				const uint64 nextDomainDue_ns = TimeUntilNextDomainDue(now_ns);
				if (nextDomainDue_ns != UINT64_MAX)
					idleWait_ns = std::min(idleWait_ns, nextDomainDue_ns);
				const uint64 nextFiberWakeup_ns = m_scheduler->NextWakeupTime();
				if (nextFiberWakeup_ns != 0)
				{
					idleWait_ns = (now_ns < nextFiberWakeup_ns)
						? std::min(idleWait_ns, nextFiberWakeup_ns - now_ns)
						: 0_ns;
				}
				WaitForWorkOrTimeout(observedWakeEpoch, idleWait_ns);
				m_metrics.idleSleepCost_ns += NOW_NS() - idleStart_ns;
			}
			else
			{
				++m_metrics.didWorkLoopCount;
			}
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
			}
		}

		mb->EndConsume();

		if (mb->TryFinalizeClose())
		{
			RemoveMailbox(mb->GetId());
			return movedCount;
		}

		if ((mb->ConsumeRepostRequested() || !mb->IsEmpty()) && mb->TryBeginConsume())
		{
			if (!NotifyReady(mb->GetId()))
			{
				mb->RequestRepost();
				mb->EndConsume();
			}
		}

		return movedCount;
	}


	void ShardExecutor::DrainReadyMailboxes(int32 maxMailboxes, int32 totalJobBudget, int32 budgetPerMailbox)
	{
		if (maxMailboxes <= 0 || totalJobBudget <= 0 || budgetPerMailbox <= 0)
			return;

		uint32 mailboxId = 0;
		int32 remainingJobBudget = totalJobBudget;

		for (int32 i = 0; i < maxMailboxes && remainingJobBudget > 0; ++i)
		{
			if (!m_readyMailboxes.try_dequeue(mailboxId))
				break;
			if (mailboxId == 0)
				continue;

			auto mailbox = FindMailbox(mailboxId);
			if (!mailbox)
				continue;

			++m_metrics.mailboxProcessCount;
			const int32 mailboxBudget = std::min(budgetPerMailbox, remainingJobBudget);
			const uint64 moved = ProcessMailbox(mailbox, mailboxBudget);
			remainingJobBudget -= static_cast<int32>(std::min<uint64>(moved, static_cast<uint64>(remainingJobBudget)));
		}

		if (remainingJobBudget < 0)
			JAMNET_LOG_WARN("remaining job budget < 0");
	}

	Mailbox* ShardExecutor::FindMailbox(uint32 id)
	{
		if (id == 0)
			return nullptr;

		READ_LOCK
		auto it = m_mailboxes.find(id);
		return (it != m_mailboxes.end()) ? it->second.get() : nullptr;
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
