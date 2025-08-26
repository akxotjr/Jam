#pragma once
#include "concurrentqueue/blockingconcurrentqueue.h"
#include "concurrentqueue/concurrentqueue.h"
#include "Job.h"

namespace jam::utils::exec
{
	class ShardExecutor;
	class ShardEndpoint;


	struct GlobalExecutorConfig
	{
		// 0이면 자동 산정(권장). 자동 산정은 논리 코어 수와 샤드 수를 고려.
		uint32 workers = 0;

		// 자동 산정 시 범위
		uint32 minWorkers = 1;
		uint32 maxWorkers = UINT32_MAX;

		// 자동 튜닝(옵션): 큐 길이/지연 기반으로 런타임 조정할 때 사용 가능
		bool   autoTune = false;

		uint64 capacity = 1 << 16;
	};

	class GlobalExecutor
	{
	public:
		GlobalExecutor(const GlobalExecutorConfig& config = {});
		~GlobalExecutor();

		void				Start(const xvector<Sptr<ShardExecutor>>& shards);
		void				Stop();
		void				Join();	

		void				Post(job::Job job);
		void				PostAfter(job::Job job, uint64 delay_ns);

		void				RequestAssist(uint32 shardIndex);

		// shard/endpoint
		uint32				GetShardCount() const { return static_cast<uint32>(m_shards.size()); }
		Sptr<ShardExecutor> GetShard(uint32 index) const;


	private:
		void				WorkerLoop();
		void				TimerLoop();

		uint32				ComputeWorkerCount(uint32 shardCount) const;

	private:
		GlobalExecutorConfig									m_config;
		Atomic<bool>											m_running{ false };

		// offload (MPMC)
		moodycamel::BlockingConcurrentQueue<job::Job>			m_offload;

		// assist (MPMC)
		moodycamel::ConcurrentQueue<uint32>						m_assist;	// shard index

		// shard list
		xvector<Sptr<ShardExecutor>>							m_shards;

		// worker
		xvector<std::thread>									m_workers;

		// timer
		std::thread												m_timerThread;

		struct TimedItem
		{
			uint64		due_ns; // (ns)
			job::Job	job;
		};
		std::mutex												m_timerMutex;
		std::condition_variable									m_timerCv;
		xvector<TimedItem>										m_timedItems;

	};
}
