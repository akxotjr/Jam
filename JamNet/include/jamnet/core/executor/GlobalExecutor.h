#pragma once

#include "IExecutor.h"

#include "ShardExecutor.h"
#include "ShardDirectory.h"
#include "CoreTopology.h"

#include "FiberScheduler.h"


namespace jam
{
	class ShardEndpoint;


	struct GlobalExecutorConfig
	{
		bool					autoTune = true;
		CoreLayout				layout;
		AutoCoreLayoutConfig	layoutCfg;

		ShardExecutorConfig		shardCfg;
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
		shared_ptr<ShardExecutor>			GetShard(uint32 index) const { return m_directory ? m_directory->ShardAt(index) : nullptr; }
		shared_ptr<ShardExecutor>			GetShard(uint64 key) const { return m_directory ? m_directory->ShardAt(m_directory->PickShard(key)) : nullptr; }
		vector<shared_ptr<ShardExecutor>>&	GetShards() const { return m_directory->Shards(); }

		shared_ptr<ShardDirectory>			GetDirectory() const { return m_directory; }

		RouteKey							MakeRouteKey(string_view domain, uint64 id) const { return m_routing.KeyForAffinity(domain, id); }
		shared_ptr<ShardExecutor>			GetShard(RouteKey rk) const { return GetShard(rk.value()); }
		
		void								ConveyAll(Job j);		// Convey job to all shards


		// GE용 Fiber API
		void								SpawnFiber(FiberFn fn, const FiberDesc& desc = {}) const;
		void								ResumeFiber(AwaitKey key) const;
		void								CancelFiberByKey(AwaitKey key, eCancelCode code) const;
		void								CancelFiberById(uint32 id, eCancelCode code) const;


		struct PeriodicHandle { uint32 id = 0; };
		struct PeriodicOptions
		{
			uint64			period_ns = 0;
			uint64			initialDelay_ns = 0;
			int32			maxCatchUp = 0;
			const char*		name = "G_EXEC.Periodic";
		};

		PeriodicHandle						ScheduleFixedRate(Job j, const PeriodicOptions& opt);
		PeriodicHandle						ScheduleFixedDelay(Job j, const PeriodicOptions& opt);
		bool								CancelPerioidc(PeriodicHandle h);

	private:
		void								WorkerLoop();
		void								FiberLoop();

	private:

		USE_LOCK


		GlobalExecutorConfig							m_config;
		Atomic<bool>									m_running{ false };

		// offload (MPMC)	
		BlockingConcurrentQueue<Job>					m_offload;

		// worker
		vector<std::thread>								m_workers;
		shared_ptr<ShardDirectory>						m_directory;

		RoutingPolicy									m_routing{ RandomSeed() };


		WinFiberBackend									m_backend;
		unique_ptr<FiberScheduler>						m_scheduler;
		std::thread										m_fiberThread;
		atomic<uint64>									m_nextAwaitSeq{ 1 };


		struct PeriodicState
		{
			std::atomic<bool>	cancelled{ false };
			uint64				period_ns{ 0 };
			uint64				initialDelay_ns{ 0 };
			int32				maxCatchUp{ 0 };
			bool				fixedRate{ true };
		};

		atomic<uint32>									m_periodicId{ 1 };
		unordered_map<uint32, weak_ptr<PeriodicState>>	m_periodics;
	};

}


#define GLOBAL_EXEC				jam::GlobalExecutor::Instance()
#define GLOBAL_EXEC_INIT(cfg)	jam::GlobalExecutor::Instance().Init(cfg)