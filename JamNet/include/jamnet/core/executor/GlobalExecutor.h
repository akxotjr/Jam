#pragma once

#include "jamnet/core/executor/IExecutor.h"
#include "jamnet/core/executor/ShardExecutor.h"
#include "jamnet/core/executor/ShardDirectory.h"
#include "jamnet/core/executor/ShardRoutingPolicy.h"
#include "jamnet/core/executor/CoreTopology.h"
#include "jamnet/core/executor/ExecutorMetrics.h"
#include "jamnet/core/executor/ExecutorPeriodic.h"
#include "jamnet/core/executor/FiberScheduler.h"
#include "jamnet/core/executor/SeqLock.h"

#include "jamnet/core/net/IocpCore.h"

#include <condition_variable>
#include <mutex>


namespace jam
{

	struct GlobalExecutorConfig
	{
		bool					autoTune  = true;
		CoreLayout				layout	  = {};
		AutoCoreLayoutConfig	layoutCfg = {};
		ExecutorAffinityConfig	affinity  = {};
		RouteSeed				routeSeed = {};

		ShardExecutorConfig		shardCfg  = {};
	};


	class GlobalExecutor : public IExecutor
	{
		DECLARE_SINGLETON_INHERITANCE(GlobalExecutor)


	public:
		void											Init(const GlobalExecutorConfig& config);
		void											ShutDown();


		void											Start();
		void											Stop();
		void											Join();	

		void											Submit(Job j) override;
		void											SubmitAfter(Job j, uint64 delay_ns);

		// shard/endpoint
		uint32											GetShardCount() const { return m_directory ? static_cast<uint32>(m_directory->Size()) : 0; }
		std::shared_ptr<ShardExecutor>					GetShard(uint32 index) const { return m_directory ? m_directory->ShardAt(index) : nullptr; }
		std::shared_ptr<ShardExecutor>					GetShard(uint64 key)   const { return GetShard(RouteKey(key)); }
		std::shared_ptr<ShardExecutor>					GetShard(RouteKey rk)  const { return m_directory && IsValidRouteKey(rk) ? m_directory->ShardAt(m_directory->PickShard(rk)) : nullptr; }
		std::shared_ptr<ShardExecutor>					GetShard(const RouteAssignment& assignment) const { return IsValidRouteAssignment(assignment) ? GetShard(assignment.shardIndex) : nullptr; }

		std::vector<std::shared_ptr<ShardExecutor>>&	GetShards() const { return m_directory->Shards(); }

		size_t											GetQueueSize() const { return m_offload.size_approx(); }

		std::shared_ptr<ShardDirectory>					GetDirectory() const { return m_directory; }
		std::shared_ptr<net::IocpCore>					AcquireIocpCore();

		RouteKey										MakeRouteKey(RouteDomain domain, uint64 id) const { return m_directory ? m_directory->MakeRouteKey(domain, id) : RouteKey{}; }
		RouteKey										MakeRouteKey(std::string_view domain, uint64 id) const { return m_directory ? m_directory->MakeRouteKey(domain, id) : RouteKey{}; }
		RouteAssignment									PlaceRoute(RouteKey key, const RoutePlacementOptions& opt = {}) const;
		void											ReleaseRoute(const RouteAssignment& assignment) const;
		
		void											ConveyAll(Job j);		// Convey job to all shards

		void											SpawnFiber(FiberFn fn, const FiberDesc& desc = {}) const;
		void											ResumeFiber(FiberAwaitKey key) const;
		void											CancelFiberByKey(FiberAwaitKey key, eCancelCode code) const;
		void											CancelFiberById(uint32 id, eCancelCode code) const;


		PeriodicHandle									ScheduleFixedRate(Job j, const PeriodicOptions& opt);
		PeriodicHandle									ScheduleFixedDelay(Job j, const PeriodicOptions& opt);
		bool											CancelPeriodic(PeriodicHandle h);
		bool											CancelPerioidc(PeriodicHandle h) { return CancelPeriodic(h); }

		GlobalExecutorMetrics							GetMetricsSnapshot() const;
		std::vector<ShardExecutorMetrics>				GetShardMetricsSnapshots() const;
		void											ResetMetrics();

	private:
		struct OffloadWorkerMetrics
		{
			uint64	loopCount		= 0;
			uint64	jobExecCount	= 0;
			uint64	idleLoopCount	= 0;
			uint64	waitCost_ns		= 0_ns;
			uint64	jobExecCost_ns	= 0_ns;
		};

		struct FiberWorkerMetrics
		{
			uint64	loopCount		= 0;
			uint64	pollCount		= 0;
			uint64	emptyPollCount	= 0;
			uint64	pollCost_ns		= 0_ns;
			uint64	sleepCost_ns	= 0_ns;
			uint64	readyRunCount	= 0;
		};

		template <typename TMetrics>
		struct MetricsSlot
		{
			SeqLock			seq;
			TMetrics		value = {};
		};

		struct IocpDomain;

		void									OffloadWorkerLoop();
		void									IocpWorkerLoop(std::shared_ptr<IocpDomain> domain);
		void									FiberLoop();
		void									NotifyFiberWorkAvailable() const;
		void									WaitForFiberWorkOrTimeout(uint64 observedWakeEpoch, uint64 timeout_ns) const;

		void									StartIocpDomain(const std::shared_ptr<IocpDomain>& domain);
		void									StopIocpDomain(const std::shared_ptr<IocpDomain>& domain);
		void									JoinIocpDomain(const std::shared_ptr<IocpDomain>& domain);
		void									BuildAffinityPlan();
		bool									TryGetAffinitySlot(uint32 slotIndex, ThreadAffinitySlot& out) const;
		void									PinCurrentThreadForRole(std::string_view roleName, uint32 roleIndex, uint32 slotIndex) const;
		uint32									OffloadAffinitySlotBase() const;
		uint32									FiberAffinitySlotBase() const;
		uint32									IocpAffinitySlotBase() const;
		uint32									MainAffinitySlotIndex() const;

		OffloadWorkerMetrics					GetOffloadMetricsSnapshot(const MetricsSlot<OffloadWorkerMetrics>& slot) const;
		FiberWorkerMetrics						GetFiberMetricsSnapshot(const MetricsSlot<FiberWorkerMetrics>& slot) const;
		GlobalExecutorMetrics					GetMetricsSnapshotRaw() const;
		static GlobalExecutorMetrics			SubtractMetrics(const GlobalExecutorMetrics& value, const GlobalExecutorMetrics& baseline);

	private:

		USE_LOCK


		GlobalExecutorConfig									m_config					= {};
		Atomic<bool>											m_running					= false;

		// offload (MPMC)	
		BlockingConcurrentQueue<Job>							m_offload;

		// worker
		std::vector<std::thread>								m_offloadWorkers;
		std::vector<std::shared_ptr<IocpDomain>>				m_iocpDomains;
		std::shared_ptr<ShardDirectory>							m_directory					= nullptr;

		WinFiberBackend											m_backend;
		std::unique_ptr<FiberScheduler>							m_scheduler					= nullptr;
		std::thread												m_fiberWorker;
		mutable std::mutex										m_fiberWakeMutex;
		mutable std::condition_variable							m_fiberWakeCv;
		mutable std::atomic<uint64>								m_fiberWakeEpoch			= 0;

		std::atomic<uint64>										m_nextAwaitSeq				= 0;
		mutable std::atomic<uint32>								m_nextIocpDomain			= 0;

		std::atomic<uint32>										m_nextOffloadWorkerSlot		= 0;
		std::unique_ptr<MetricsSlot<OffloadWorkerMetrics>[]>	m_offloadMetricSlots;
		size_t													m_offloadMetricSlotCount	= 0;
		MetricsSlot<FiberWorkerMetrics>							m_fiberMetricSlot			= {};
		SeqLockBox<GlobalExecutorMetrics>						m_metricBaseline			= {};
		std::vector<ThreadAffinitySlot>							m_affinitySlots;


		struct PeriodicState
		{
			std::atomic<bool>	cancelled		= false;
			uint64				period_ns		= 0_ns;
			uint64				initialDelay_ns = 0_ns;
			int32				maxCatchUp		= 0;
			bool				fixedRate		= true;
			FiberAwaitKey		awaitKey		= 0;
		};

		std::atomic<uint32>											m_periodicId			= 1;
		std::unordered_map<uint32, std::weak_ptr<PeriodicState>>	m_periodics;
	};

}


#define GLOBAL_EXEC				jam::GlobalExecutor::Instance()
#define GLOBAL_EXEC_INIT(cfg)	jam::GlobalExecutor::Instance().Init(cfg)
#define GLOBAL_EXEC_SHUTDOWN()  jam::GlobalExecutor::Instance().ShutDown()
