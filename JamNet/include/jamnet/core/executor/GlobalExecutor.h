#pragma once

#include "jamnet/core/executor/IExecutor.h"
#include "jamnet/core/executor/ShardExecutor.h"
#include "jamnet/core/executor/ShardDirectory.h"
#include "jamnet/core/executor/CoreTopology.h"
#include "jamnet/core/executor/ExecutorMetrics.h"
#include "jamnet/core/executor/FiberScheduler.h"
#include "jamnet/core/executor/SeqLock.h"
#include "jamnet/core/net/IocpCore.h"


namespace jam
{
	class ShardEndpoint;


	struct GlobalExecutorConfig
	{
		bool					autoTune  = true;
		CoreLayout				layout	  = {};
		AutoCoreLayoutConfig	layoutCfg = {};

		ShardExecutorConfig		shardCfg  = {};
	};


	class GlobalExecutor : public IExecutor
	{
		DECLARE_SINGLETON_INHERITANCE(GlobalExecutor)


	public:
		void								Init(const GlobalExecutorConfig& config);
		void								ShutDown();


		void								Start();
		void								Stop();
		void								Join();	

		void								Submit(Job j) override;
		void								SubmitAfter(Job j, uint64 delay_ns);

		// shard/endpoint
		uint32								GetShardCount() const { return m_directory ? static_cast<uint32>(m_directory->Size()) : 0; }
		std::shared_ptr<ShardExecutor>		GetShard(uint32 index) const { return m_directory ? m_directory->ShardAt(index) : nullptr; }
		std::shared_ptr<ShardExecutor>		GetShard(uint64 key)   const { return m_directory ? m_directory->ShardAt(m_directory->PickShard(key)) : nullptr; }
		std::shared_ptr<ShardExecutor>		GetShard(RouteKey rk)  const { return GetShard(rk.value()); }

		std::vector<std::shared_ptr<ShardExecutor>>&	GetShards() const { return m_directory->Shards(); }

		size_t								GetQueueSize() const { return m_offload.size_approx(); }

		std::shared_ptr<ShardDirectory>		GetDirectory() const { return m_directory; }
		std::shared_ptr<net::IocpCore>		AcquireIocpCore();

		RouteKey							MakeRouteKey(std::string_view domain, uint64 id) const { return m_routing.KeyForAffinity(domain, id); }
		
		void								ConveyAll(Job j);		// Convey job to all shards


		// GE용 Fiber API
		void								SpawnFiber(FiberFn fn, const FiberDesc& desc = {}) const;
		void								ResumeFiber(FiberAwaitKey key) const;
		void								CancelFiberByKey(FiberAwaitKey key, eCancelCode code) const;
		void								CancelFiberById(uint32 id, eCancelCode code) const;


		struct PeriodicHandle { uint32 id = 0; };
		struct PeriodicOptions
		{
			uint64			period_ns		= 0;
			uint64			initialDelay_ns = 0;
			int32			maxCatchUp		= 0;
			const char*		name			= "G_EXEC.Periodic";
		};

		PeriodicHandle							ScheduleFixedRate(Job j, const PeriodicOptions& opt);
		PeriodicHandle							ScheduleFixedDelay(Job j, const PeriodicOptions& opt);
		bool									CancelPerioidc(PeriodicHandle h);

		GlobalExecutorMetrics					GetMetricsSnapshot() const;
		std::vector<ShardExecutorMetrics>		GetShardMetricsSnapshots() const;
		void									ResetMetrics();

	private:
		struct OffloadWorkerMetrics
		{
			uint64	loopCount		= 0;
			uint64	jobExecCount	= 0;
			uint64	idleLoopCount	= 0;
			uint64	waitCost_ns		= 0;
			uint64	jobExecCost_ns	= 0;
		};

		struct FiberWorkerMetrics
		{
			uint64	loopCount		= 0;
			uint64	pollCount		= 0;
			uint64	emptyPollCount	= 0;
			uint64	pollCost_ns		= 0;
			uint64	sleepCost_ns	= 0;
			uint64	readyRunCount	= 0;
		};

		template <typename TMetrics>
		struct MetricsSlot
		{
			net::SeqLock	seq;
			TMetrics		value = {};
		};

		struct IocpDomain;

		void									OffloadWorkerLoop();
		void									IocpWorkerLoop(std::shared_ptr<IocpDomain> domain);
		void									FiberLoop();

		void									StartIocpDomain(const std::shared_ptr<IocpDomain>& domain);
		void									StopIocpDomain(const std::shared_ptr<IocpDomain>& domain);
		void									JoinIocpDomain(const std::shared_ptr<IocpDomain>& domain);

		OffloadWorkerMetrics					GetOffloadMetricsSnapshot(const MetricsSlot<OffloadWorkerMetrics>& slot) const;
		FiberWorkerMetrics						GetFiberMetricsSnapshot(const MetricsSlot<FiberWorkerMetrics>& slot) const;
		GlobalExecutorMetrics					GetMetricsSnapshotRaw() const;
		static GlobalExecutorMetrics			SubtractMetrics(const GlobalExecutorMetrics& value, const GlobalExecutorMetrics& baseline);

	private:

		USE_LOCK


		GlobalExecutorConfig									m_config;
		Atomic<bool>											m_running{ false };

		// offload (MPMC)	
		BlockingConcurrentQueue<Job>							m_offload;

		// worker
		std::vector<std::thread>								m_offloadWorkers;
		std::vector<std::shared_ptr<IocpDomain>>				m_iocpDomains;
		std::shared_ptr<ShardDirectory>							m_directory;

		RoutingPolicy											m_routing{ RandomSeed() };

		WinFiberBackend											m_backend;
		std::unique_ptr<FiberScheduler>							m_scheduler;
		std::thread												m_fiberWorker;

		std::atomic<uint64>										m_nextAwaitSeq{ 1 };
		mutable std::atomic<uint32>								m_nextIocpDomain{ 0 };

		std::atomic<uint32>										m_nextOffloadWorkerSlot{ 0 };
		std::unique_ptr<MetricsSlot<OffloadWorkerMetrics>[]>	m_offloadMetricSlots;
		size_t													m_offloadMetricSlotCount = 0;
		MetricsSlot<FiberWorkerMetrics>							m_fiberMetricSlot = {};
		net::SeqLockBox<GlobalExecutorMetrics>					m_metricBaseline = {};


		struct PeriodicState
		{
			std::atomic<bool>	cancelled{ false };
			uint64				period_ns{ 0 };
			uint64				initialDelay_ns{ 0 };
			int32				maxCatchUp{ 0 };
			bool				fixedRate{ true };
		};

		std::atomic<uint32>											m_periodicId{ 1 };
		std::unordered_map<uint32, std::weak_ptr<PeriodicState>>	m_periodics;
	};

}


#define GLOBAL_EXEC				jam::GlobalExecutor::Instance()
#define GLOBAL_EXEC_INIT(cfg)	jam::GlobalExecutor::Instance().Init(cfg)
#define GLOBAL_EXEC_SHUTDOWN()  jam::GlobalExecutor::Instance().ShutDown()
