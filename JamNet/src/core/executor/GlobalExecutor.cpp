#include "pch.h"
#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/core/executor/GlobalExecutor.h"
#include "jamnet/core/executor/ShardDirectory.h"
#include "jamnet/core/executor/ShardExecutor.h"
#include "jamnet/core/net/AdmissionContext.h"

namespace jam
{
	namespace
	{
		inline const RouteDomain kShardAffinityRouteDomain = RouteDomain::From("ShardAffinity");
	}

	struct GlobalExecutor::IocpDomain
	{
		uint32									id = 0;
		std::shared_ptr<net::IocpCore>			core;
		std::shared_ptr<net::AdmissionContext>	admission;
		std::atomic<bool>						active{ true };
		std::thread								worker;
	};

	void GlobalExecutor::Init(const GlobalExecutorConfig& config)
	{
		m_config = config;

		m_config.layout = m_config.autoTune ? AutoLayout(m_config.layoutCfg) : m_config.layout;
		if (m_config.affinity.useProfileDefaults)
			m_config.affinity = DefaultExecutorAffinityConfig(m_config.layoutCfg.profile);
		JAM_ASSERT(m_config.layout.IsValid());

		BuildAffinityPlan();

		ShardDirectoryConfig dirCfg = {
			.numShards			= static_cast<uint32>(m_config.layout.shards),
			.shardCfg			= m_config.shardCfg,
			.routeSeed			= m_config.routeSeed,
			.pinShardWorkers	= m_config.affinity.pinShardWorkers,
			.affinitySlots		= m_affinitySlots,
			.affinitySlotOffset = 0
		};

		m_directory = std::make_shared<ShardDirectory>(dirCfg);
		m_directory->Init();

		m_iocpDomains.clear();
		m_iocpDomains.reserve(static_cast<size_t>(m_config.layout.iocp));
		for (int32 i = 0; i < m_config.layout.iocp; ++i)
		{
			auto domain = std::make_shared<IocpDomain>();
			domain->id   = static_cast<uint32>(i);
			domain->core = std::make_shared<net::IocpCore>();
			domain->admission = std::make_shared<net::AdmissionContext>(domain->core.get());

			m_iocpDomains.push_back(domain);
		}

		m_nextIocpDomain.store(0, std::memory_order_relaxed);
		m_nextOffloadWorkerSlot.store(0, std::memory_order_relaxed);

		m_offloadMetricSlotCount = (m_config.layout.offload > 0) ? static_cast<size_t>(m_config.layout.offload) : 0;
		m_offloadMetricSlots.reset();
		if (m_offloadMetricSlotCount != 0)
			m_offloadMetricSlots = std::make_unique<MetricsSlot<OffloadWorkerMetrics>[]>(m_offloadMetricSlotCount);

		m_fiberMetricSlot.value = {};
		m_metricsAggregator = std::make_unique<MetricsAggregator>();
		if (!m_metricsAggregator->Initialize(m_config.metrics))
		{
			JAM_LOG_ERROR("Failed to initialize MetricsAggregator output");
			m_metricsAggregator.reset();
		}
		const uint64 processWindowIndex = m_metricsAggregator && m_metricsAggregator->IsEnabled()
			? m_metricsAggregator->WindowIndex(NOW_NS())
			: UINT64_MAX;
		m_processMetrics.Initialize(std::max<DWORD>(1, GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)), processWindowIndex);
		m_executorMetricsWindowIndex = processWindowIndex;
		m_executorMetricsBaseline = CaptureExecutorMetrics();

		m_backend   = WinFiberBackend();
		m_scheduler = std::make_unique<FiberScheduler>(m_backend);
	}

	void GlobalExecutor::ShutDown()
	{
		Stop();
		Join();

		{
			std::scoped_lock lock(m_offloadLifecycleMutex);
			Job discarded;
			while (m_offload.try_dequeue(discarded))
			{
			}
		}

		{
			WRITE_LOCK
			m_periodics.clear();
			m_iocpDomains.clear();
		}

		// ShardLocal owns session/world/user state and shard schedulers own delayed
		// jobs. Release the stopped directory here so a later Init cannot destroy
		// state belonging to the previous runtime.
		m_directory.reset();
		m_affinitySlots.clear();
		if (m_metricsAggregator)
		{
			m_metricsAggregator->Shutdown();
			m_metricsAggregator.reset();
		}
		m_offloadMetricSlots.reset();
		m_offloadMetricSlotCount = 0;
	}

	void GlobalExecutor::Start()
	{
		if (m_running.exchange(true, std::memory_order_release))
			return;

		if (!m_scheduler)
		{
			m_backend   = WinFiberBackend();
			m_scheduler = std::make_unique<FiberScheduler>(m_backend);
		}

		m_nextOffloadWorkerSlot.store(0, std::memory_order_relaxed);
		m_fiberWakeEpoch.store(0, std::memory_order_relaxed);
		if (m_config.affinity.pinMainThread)
			PinCurrentThreadForRole("Main", 0, MainAffinitySlotIndex());

		m_directory->Start();
		if (m_metricsAggregator && m_metricsAggregator->IsEnabled())
		{
			const uint64 periodNs = m_metricsAggregator->WindowPeriodNs();
			m_processMetricsPeriodic = ScheduleFixedRate(Job([this]
				{
					if (m_metricsAggregator)
					{
						m_processMetrics.SubmitCompletedWindow(NOW_NS(), *m_metricsAggregator);
						SubmitExecutorMetricsWindow(NOW_NS());
					}
				}), PeriodicOptions{ .period_ns = periodNs, .initialDelay_ns = periodNs, .name = "GE.ProcessMetrics" });
		}

		std::vector<std::shared_ptr<IocpDomain>> domains;
		{
			READ_LOCK
			domains = m_iocpDomains;
		}

		for (const auto& domain : domains)
			StartIocpDomain(domain);

		m_fiberWorker = std::thread(&GlobalExecutor::FiberLoop, this);

		m_offloadWorkers.reserve(m_config.layout.offload);
		for (int32 i = 0; i < m_config.layout.offload; ++i)
		{
			m_offloadWorkers.emplace_back(&GlobalExecutor::OffloadWorkerLoop, this);
		}
	}

	void GlobalExecutor::Stop()
	{
		if (!m_running.exchange(false))
			return;

		m_directory->StopAll();

		{
			WRITE_LOCK
			for (auto& wp : m_periodics | std::views::values)
				if (auto st = wp.lock())
					st->cancelled.store(true, std::memory_order_relaxed);
		}
		NotifyFiberWorkAvailable();

		for (size_t i = 0; i < m_offloadWorkers.size(); ++i)
			m_offload.enqueue(Job([] {}));

		std::vector<std::shared_ptr<IocpDomain>> domains;
		{
			READ_LOCK
			domains = m_iocpDomains;
		}

		for (const auto& domain : domains)
			StopIocpDomain(domain);
	}

	void GlobalExecutor::Join()
	{
		m_directory->JoinAll();

		for (auto& t : m_offloadWorkers)
			if (t.joinable()) t.join();

		m_offloadWorkers.clear();

		std::vector<std::shared_ptr<IocpDomain>> domains;
		{
			READ_LOCK
			domains = m_iocpDomains;
		}

		for (const auto& domain : domains)
			JoinIocpDomain(domain);

		if (m_fiberWorker.joinable())
			m_fiberWorker.join();

		m_scheduler.reset();
	}

	IocpBinding GlobalExecutor::AcquireIocpBinding()
	{
		std::shared_ptr<IocpDomain> domain;
		{
			READ_LOCK
			if (m_iocpDomains.empty())
				return {};

			const uint32 index = m_nextIocpDomain.fetch_add(1, std::memory_order_relaxed) % static_cast<uint32>(m_iocpDomains.size());
			domain = m_iocpDomains[index];
		}

		if (!domain)
			return {};

		return IocpBinding{
			.core		= domain->core,
			.admission	= domain->admission,
		};
	}

	std::shared_ptr<net::IocpCore> GlobalExecutor::AcquireIocpCore()
	{
		return AcquireIocpBinding().core;
	}

	RouteKey GlobalExecutor::MakeAffinityRouteKey(uint64 seed) const
	{
		return MakeRouteKey(kShardAffinityRouteDomain, seed);
	}

	std::shared_ptr<ShardExecutor> GlobalExecutor::GetAffinityShard(uint64 seed) const
	{
		return GetShard(MakeAffinityRouteKey(seed));
	}

	RouteAssignment GlobalExecutor::PlaceRoute(RouteKey key, const RoutePlacementOptions& opt) const
	{
		return m_directory ? m_directory->PlaceRoute(key, opt) : RouteAssignment{};
	}

	void GlobalExecutor::ReleaseRoute(const RouteAssignment& assignment) const
	{
		if (m_directory)
			m_directory->ReleaseRoute(assignment);
	}

	void GlobalExecutor::ReleaseRoute(uint16 shardIndex)
	{
		if (m_directory)
			m_directory->ReleaseRoute(shardIndex);
	}

	void GlobalExecutor::Submit(Job j)
	{
		std::scoped_lock lock(m_offloadLifecycleMutex);
		if (!m_running.load(std::memory_order_acquire))
			return;

		m_offload.enqueue(std::move(j));
	}

	void GlobalExecutor::SubmitMetrics(MetricSnapshot snapshot)
	{
		if (!m_metricsAggregator || !m_metricsAggregator->IsEnabled())
			return;

		Submit(Job([this, snapshot = std::move(snapshot)]() mutable
			{
				if (m_metricsAggregator)
					m_metricsAggregator->Submit(std::move(snapshot));
			}));
	}

	void GlobalExecutor::SubmitAfter(Job j, uint64 delay_ns)
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
			FiberDesc{ .name = "GE.SubmitAfter" }
		);
		NotifyFiberWorkAvailable();
	}

	void GlobalExecutor::ConveyAll(Job j)
	{
		if (!m_directory) return;

		auto& shards = m_directory->Shards();
		if (shards.empty())
			return;

		auto sharedJob = std::make_shared<Job>(std::move(j));
		const eJobPriority priority = sharedJob->Priority();
		const size_t n = shards.size();
		for (size_t i = 0; i < n; ++i)
		{
			auto& sh = shards[i];
			if (!sh) continue;
			sh->Submit(Job([sharedJob]()
				{
					sharedJob->Execute();
				}, priority));
		}
	}

	void GlobalExecutor::SpawnFiber(FiberFn fn, const FiberDesc& desc) const
	{
		if (m_scheduler)
		{
			m_scheduler->PostSpawn(std::move(fn), desc);
			NotifyFiberWorkAvailable();
		}
	}

	void GlobalExecutor::ResumeFiber(FiberAwaitKey key) const
	{
		if (m_scheduler)
		{
			m_scheduler->PostResume(key);
			NotifyFiberWorkAvailable();
		}
	}

	void GlobalExecutor::CancelFiberByKey(FiberAwaitKey key, eCancelCode code) const
	{
		if (m_scheduler)
		{
			m_scheduler->PostCancelByKey(key, code);
			NotifyFiberWorkAvailable();
		}
	}

	void GlobalExecutor::CancelFiberById(uint32 id, eCancelCode code) const
	{
		if (m_scheduler)
		{
			m_scheduler->PostCancelById(id, code);
			NotifyFiberWorkAvailable();
		}
	}

	PeriodicHandle GlobalExecutor::ScheduleFixedRate(Job j, const PeriodicOptions& opt)
	{
		if (!m_scheduler || opt.period_ns == 0)
			return {};

		auto st = std::make_shared<PeriodicState>();
		st->period_ns = opt.period_ns;
		st->initialDelay_ns = opt.initialDelay_ns;
		st->maxCatchUp = opt.maxCatchUp;
		st->fixedRate = true;
		st->awaitKey = m_nextAwaitSeq.fetch_add(1, std::memory_order_relaxed);

		const uint32 id = m_periodicId.fetch_add(1, std::memory_order_relaxed);

		{
			WRITE_LOCK
			m_periodics[id] = st;
		}

		const char* fiberName = opt.name ? opt.name : "GE.PeriodicFixedRate";
		auto sharedJob = std::make_shared<Job>(std::move(j));
		const eJobPriority priority = sharedJob->Priority();

		m_scheduler->PostSpawn(
			[this, st, sharedJob, priority, id]
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
		NotifyFiberWorkAvailable();

		return PeriodicHandle{ id };
	}

	PeriodicHandle GlobalExecutor::ScheduleFixedDelay(Job j, const PeriodicOptions& opt)
	{
		if (!m_scheduler || opt.period_ns == 0)
			return {};

		auto st = std::make_shared<PeriodicState>();
		st->period_ns = opt.period_ns;
		st->initialDelay_ns = opt.initialDelay_ns;
		st->maxCatchUp = 0;
		st->fixedRate = false;
		st->awaitKey = m_nextAwaitSeq.fetch_add(1, std::memory_order_relaxed);

		const uint32 id = m_periodicId.fetch_add(1, std::memory_order_relaxed);

		{
			WRITE_LOCK
			m_periodics[id] = st;
		}

		const char* fiberName = opt.name ? opt.name : "GE.PeriodicFD";
		auto sharedJob = std::make_shared<Job>(std::move(j));
		const eJobPriority priority = sharedJob->Priority();

		m_scheduler->PostSpawn(
			[this, st, sharedJob, priority, id]()
			{
				// 초기 지연
				uint64 next = NOW_NS() + st->initialDelay_ns;
				m_scheduler->Suspend(st->awaitKey, next);

				while (m_running.load(std::memory_order_acquire) && !st->cancelled.load(std::memory_order_relaxed))
				{
					// 1) 실행
					Submit(Job([sharedJob]()
						{
							sharedJob->Execute();
						}, priority));
					if (!m_running.load(std::memory_order_acquire) || st->cancelled.load(std::memory_order_relaxed))
						break;

					// 2) 고정 딜레이
					m_scheduler->Suspend(st->awaitKey, NOW_NS() + st->period_ns);
				}

				// 정리
				WRITE_LOCK
				m_periodics.erase(id);
			},
			FiberDesc{ .name = fiberName }
		);
		NotifyFiberWorkAvailable();

		return PeriodicHandle{ id };
	}

	bool GlobalExecutor::CancelPeriodic(PeriodicHandle h)
	{
		WRITE_LOCK
		auto it = m_periodics.find(h.id);
		if (it == m_periodics.end()) return false;
		if (auto st = it->second.lock())
		{
			st->cancelled.store(true, std::memory_order_relaxed);
			if (m_scheduler)
			{
				m_scheduler->PostCancelByKey(st->awaitKey, eCancelCode::Manual);
				NotifyFiberWorkAvailable();
			}
			return true;
		}
		return false;
	}

	void GlobalExecutor::OffloadWorkerLoop()
	{
		const uint32 myIndex = m_nextOffloadWorkerSlot.fetch_add(1, std::memory_order_relaxed);
		JAM_ASSERT(myIndex < m_offloadMetricSlotCount);
		if (myIndex >= m_offloadMetricSlotCount)
			return;

		auto& metricSlot = m_offloadMetricSlots[myIndex];
		std::string threadName = std::format("GlobalExecutor#Offload{}", myIndex);
		InitThreadContext(threadName, this);
		if (m_config.affinity.pinOffloadWorkers)
			PinCurrentThreadForRole("Offload", myIndex, OffloadAffinitySlotBase() + myIndex);

		while (m_running.load())
		{
			Job j([] {});
			const uint64 waitStart_ns = NOW_NS();
			const bool dequeued = m_offload.wait_dequeue_timed(j, 1000);
			const uint64 waitCost_ns = NOW_NS() - waitStart_ns;
			uint64 execCost_ns = 0;

			if (dequeued)
			{
				const uint64 execStart_ns = NOW_NS();
				j.Execute();
				execCost_ns = NOW_NS() - execStart_ns;
			}

			metricSlot.seq.WriteBegin();
			++metricSlot.value.loopCount;
			metricSlot.value.waitCost_ns += waitCost_ns;
			if (dequeued)
			{
				++metricSlot.value.jobExecCount;
				metricSlot.value.jobExecCost_ns += execCost_ns;
			}
			else
			{
				++metricSlot.value.idleLoopCount;
			}
			metricSlot.seq.WriteEnd();
		}
	}

	void GlobalExecutor::StartIocpDomain(const std::shared_ptr<IocpDomain>& domain)
	{
		if (!m_running.load(std::memory_order_acquire) || !domain || !domain->core || domain->worker.joinable())
			return;

		domain->active.store(true, std::memory_order_release);
		domain->worker = std::thread(&GlobalExecutor::IocpWorkerLoop, this, domain);
	}

	void GlobalExecutor::StopIocpDomain(const std::shared_ptr<IocpDomain>& domain)
	{
		if (!domain || !domain->core)
			return;

		domain->active.store(false, std::memory_order_release);
		domain->core->Wake();
	}

	void GlobalExecutor::JoinIocpDomain(const std::shared_ptr<IocpDomain>& domain)
	{
		if (!domain || !domain->worker.joinable())
			return;

		if (domain->worker.get_id() == std::this_thread::get_id())
			domain->worker.detach();
		else
			domain->worker.join();
	}

	void GlobalExecutor::IocpWorkerLoop(std::shared_ptr<IocpDomain> domain)
	{
		const std::string threadName = std::format("GlobalExecutor#IOCP{}", domain ? domain->id : 0);
		InitThreadContext(threadName, this);
		if (m_config.affinity.pinIocpWorkers && domain)
			PinCurrentThreadForRole("IOCP", domain->id, IocpAffinitySlotBase() + domain->id);

		while (m_running.load(std::memory_order_acquire) && domain && domain->active.load(std::memory_order_acquire))
		{
			if (!domain->core)
				return;

			if (!domain->core->Dispatch(INFINITE)
				&& (!m_running.load(std::memory_order_acquire) || !domain->active.load(std::memory_order_acquire)))
				break;
		}
	}

	void GlobalExecutor::FiberLoop()
	{
		InitThreadContext("GlobalExecutor#Fiber", this);
		if (m_config.affinity.pinFiberWorker)
			PinCurrentThreadForRole("Fiber", 0, FiberAffinitySlotBase());

		m_scheduler->AttachToCurrentThread();
		auto& metricSlot = m_fiberMetricSlot;

		while (m_running.load(std::memory_order_acquire))
		{
			const uint64 observedWakeEpoch = m_fiberWakeEpoch.load(std::memory_order_acquire);
			const uint64 pollStart_ns = NOW_NS();
			m_scheduler->Poll(64, pollStart_ns);
			const uint64 pollCost_ns = NOW_NS() - pollStart_ns;

			const auto& profile = m_scheduler->Profile();
			const bool isEmptyPoll = (profile.lastPollReadyRunCount == 0);

			uint64 sleepCost_ns = 0;
			if (profile.lastPollReadyRunCount > 0)
			{
				metricSlot.seq.WriteBegin();
				++metricSlot.value.loopCount;
				++metricSlot.value.pollCount;
				metricSlot.value.pollCost_ns += pollCost_ns;
				metricSlot.value.readyRunCount = profile.readyRunCount;
				metricSlot.seq.WriteEnd();

				continue;
			}

			const uint64 idleStart_ns = NOW_NS();
			uint64 idleWait_ns = 1'000'000ull;
			const uint64 now_ns = NOW_NS();
			const uint64 nextFiberWakeup_ns = m_scheduler->NextWakeupTime();
			if (nextFiberWakeup_ns != 0)
			{
				idleWait_ns = (now_ns < nextFiberWakeup_ns)
					? std::min(idleWait_ns, nextFiberWakeup_ns - now_ns)
					: 0_ns;
			}
			WaitForFiberWorkOrTimeout(observedWakeEpoch, idleWait_ns);
			sleepCost_ns = NOW_NS() - idleStart_ns;

			metricSlot.seq.WriteBegin();
			++metricSlot.value.loopCount;
			++metricSlot.value.pollCount;
			if (isEmptyPoll)
				++metricSlot.value.emptyPollCount;
			metricSlot.value.pollCost_ns += pollCost_ns;
			metricSlot.value.sleepCost_ns += sleepCost_ns;
			metricSlot.value.readyRunCount = profile.readyRunCount;
			metricSlot.seq.WriteEnd();
		}

		m_scheduler->DetachFromThread();
	}

	void GlobalExecutor::NotifyFiberWorkAvailable() const
	{
		m_fiberWakeEpoch.fetch_add(1, std::memory_order_release);
		m_fiberWakeCv.notify_one();
	}

	void GlobalExecutor::WaitForFiberWorkOrTimeout(uint64 observedWakeEpoch, uint64 timeout_ns) const
	{
		if (timeout_ns == 0)
		{
			std::this_thread::yield();
			return;
		}

		std::unique_lock lock(m_fiberWakeMutex);
		m_fiberWakeCv.wait_for(
			lock,
			std::chrono::nanoseconds(timeout_ns),
			[this, observedWakeEpoch]()
			{
				return !m_running.load(std::memory_order_acquire)
					|| m_fiberWakeEpoch.load(std::memory_order_acquire) != observedWakeEpoch;
			});
	}

	void GlobalExecutor::BuildAffinityPlan()
	{
		m_affinitySlots.clear();

		const auto& cfg = m_config.affinity;
		if (!cfg.pinShardWorkers
			&& !cfg.pinOffloadWorkers
			&& !cfg.pinFiberWorker
			&& !cfg.pinIocpWorkers
			&& !cfg.pinMainThread)
		{
			return;
		}

		m_affinitySlots = BuildRoundRobinCoreSlots(QueryNumaNodesWithPrimaryCoreSlots());
		if (m_affinitySlots.empty())
			JAM_LOG_WARN("Executor affinity is enabled, but no physical core slots were discovered");
	}

	bool GlobalExecutor::TryGetAffinitySlot(uint32 slotIndex, ThreadAffinitySlot& out) const
	{
		if (m_affinitySlots.empty())
			return false;

		const uint32 remappedSlotIndex = RemapExecutorAffinitySlot(slotIndex);
		out = m_affinitySlots[static_cast<size_t>(remappedSlotIndex) % m_affinitySlots.size()];
		return out.core.mask != 0;
	}

	void GlobalExecutor::PinCurrentThreadForRole(std::string_view roleName, uint32 roleIndex, uint32 slotIndex) const
	{
		ThreadAffinitySlot slot = {};
		if (!TryGetAffinitySlot(slotIndex, slot))
		{
			JAM_LOG_WARN("{}#{} affinity requested but no valid core slot exists", roleName, roleIndex);
			return;
		}

		if (PinCurrentThreadTo(slot.core))
		{
			JAM_LOG_DEBUG("{}#{} pinned. numaNode={}, group={}, mask=0x{:X}",
				roleName,
				roleIndex,
				slot.numaNode,
				slot.core.group,
				static_cast<unsigned long long>(slot.core.mask));
			return;
		}

		const DWORD errorCode = GetLastError();
		JAM_LOG_WARN_LOC("{}#{} pinning failed. numaNode={}, group={}, mask=0x{:X}, error={}",
			roleName,
			roleIndex,
			slot.numaNode,
			slot.core.group,
			static_cast<unsigned long long>(slot.core.mask),
			errorCode);
	}

	uint32 GlobalExecutor::OffloadAffinitySlotBase() const
	{
		return static_cast<uint32>(std::max(0, m_config.layout.shards));
	}

	uint32 GlobalExecutor::FiberAffinitySlotBase() const
	{
		return OffloadAffinitySlotBase() + static_cast<uint32>(std::max(0, m_config.layout.offload));
	}

	uint32 GlobalExecutor::IocpAffinitySlotBase() const
	{
		return FiberAffinitySlotBase() + static_cast<uint32>(std::max(0, m_config.layout.fiber));
	}

	uint32 GlobalExecutor::MainAffinitySlotIndex() const
	{
		return IocpAffinitySlotBase() + static_cast<uint32>(std::max(0, m_config.layout.iocp));
	}

	GlobalExecutor::OffloadWorkerMetrics GlobalExecutor::GetOffloadMetricsSnapshot(const MetricsSlot<OffloadWorkerMetrics>& slot) const
	{
		OffloadWorkerMetrics snapshot{};
		for (;;)
		{
			const uint64 begin = slot.seq.ReadBegin();
			if (begin & 1u)
				continue;
			snapshot = slot.value;
			if (!slot.seq.ReadRetry(begin))
				return snapshot;
		}
	}

	GlobalExecutor::FiberWorkerMetrics GlobalExecutor::GetFiberMetricsSnapshot(const MetricsSlot<FiberWorkerMetrics>& slot) const
	{
		FiberWorkerMetrics snapshot{};
		for (;;)
		{
			const uint64 begin = slot.seq.ReadBegin();
			if (begin & 1u)
				continue;
			snapshot = slot.value;
			if (!slot.seq.ReadRetry(begin))
				return snapshot;
		}
	}

	GlobalExecutorMetrics GlobalExecutor::CaptureExecutorMetrics() const
	{
		GlobalExecutorMetrics result{};
		for (size_t i = 0; i < m_offloadMetricSlotCount; ++i)
		{
			const auto worker = GetOffloadMetricsSnapshot(m_offloadMetricSlots[i]);
			result.workerLoopCount += worker.loopCount;
			result.workerJobExecCount += worker.jobExecCount;
			result.workerIdleLoopCount += worker.idleLoopCount;
			result.workerWaitCost_ns += worker.waitCost_ns;
			result.workerJobExecCost_ns += worker.jobExecCost_ns;
		}

		const auto fiber = GetFiberMetricsSnapshot(m_fiberMetricSlot);
		result.fiberLoopCount = fiber.loopCount;
		result.fiberPollCount = fiber.pollCount;
		result.fiberEmptyPollCount = fiber.emptyPollCount;
		result.fiberPollCost_ns = fiber.pollCost_ns;
		result.fiberSleepCost_ns = fiber.sleepCost_ns;
		result.fiberReadyRunCount = fiber.readyRunCount;
		return result;
	}

	void GlobalExecutor::SubmitExecutorMetricsWindow(const uint64 now_ns)
	{
		if (!m_metricsAggregator || !m_metricsAggregator->IsEnabled())
			return;

		const uint64 currentWindowIndex = m_metricsAggregator->WindowIndex(now_ns);
		if (m_executorMetricsWindowIndex == UINT64_MAX)
		{
			m_executorMetricsWindowIndex = currentWindowIndex;
			m_executorMetricsBaseline = CaptureExecutorMetrics();
			return;
		}
		if (currentWindowIndex == m_executorMetricsWindowIndex)
			return;

		const auto current = CaptureExecutorMetrics();
		auto delta = [](const uint64 value, const uint64 baseline) { return value >= baseline ? value - baseline : 0; };
		const uint64 period_ns = m_metricsAggregator->WindowPeriodNs();
		MetricSnapshot snapshot{
			.windowIndex = m_executorMetricsWindowIndex,
			.windowStartNs = m_executorMetricsWindowIndex * period_ns,
			.windowEndNs = (m_executorMetricsWindowIndex + 1) * period_ns,
			.scope = "executor_global",
			.values = {
				{ "worker_loop_count", delta(current.workerLoopCount, m_executorMetricsBaseline.workerLoopCount) },
				{ "worker_jobs_executed", delta(current.workerJobExecCount, m_executorMetricsBaseline.workerJobExecCount) },
				{ "worker_idle_loop_count", delta(current.workerIdleLoopCount, m_executorMetricsBaseline.workerIdleLoopCount) },
				{ "worker_wait_ns", delta(current.workerWaitCost_ns, m_executorMetricsBaseline.workerWaitCost_ns) },
				{ "worker_job_execution_ns", delta(current.workerJobExecCost_ns, m_executorMetricsBaseline.workerJobExecCost_ns) },
				{ "fiber_loop_count", delta(current.fiberLoopCount, m_executorMetricsBaseline.fiberLoopCount) },
				{ "fiber_poll_count", delta(current.fiberPollCount, m_executorMetricsBaseline.fiberPollCount) },
				{ "fiber_empty_poll_count", delta(current.fiberEmptyPollCount, m_executorMetricsBaseline.fiberEmptyPollCount) },
				{ "fiber_poll_ns", delta(current.fiberPollCost_ns, m_executorMetricsBaseline.fiberPollCost_ns) },
				{ "fiber_sleep_ns", delta(current.fiberSleepCost_ns, m_executorMetricsBaseline.fiberSleepCost_ns) },
				{ "fiber_ready_runs", delta(current.fiberReadyRunCount, m_executorMetricsBaseline.fiberReadyRunCount) },
			}
		};
		SubmitMetrics(std::move(snapshot));
		m_executorMetricsBaseline = current;
		m_executorMetricsWindowIndex = currentWindowIndex;
	}

}
