#pragma once
#include "concurrentqueue/blockingconcurrentqueue.h"
#include "concurrentqueue/concurrentqueue.h"
#include "Job.h"
#include "ShardExecutor.h"
#include "ShardDirectory.h"
#include "CoreTopology.h"


namespace jam::utils::exec
{
	class ShardEndpoint;


	struct GlobalExecutorConfig
	{
		sys::CoreLayout layout;
		sys::AutoLayoutConfig layoutCfg;

		// 자동 튜닝(옵션): 큐 길이/지연 기반으로 런타임 조정할 때 사용 가능
		bool   autoTune = false;

		ShardExecutorConfig shardCfg;

		uint64 capacity = 1 << 16;
	};

	class GlobalExecutor : public std::enable_shared_from_this<GlobalExecutor>
	{
	public:
		GlobalExecutor(const GlobalExecutorConfig& config = {});
		~GlobalExecutor();

		void				Init(const std::vector<Sptr<ShardExecutor>>& shards = {});

		void				Start();
		void				Stop();
		void				Join();	

		void				Post(job::Job job);
		void				PostAfter(job::Job job, uint64 delay_ns);

		void				RequestAssist(uint32 shardIndex);

		// shard/endpoint
		uint32				GetShardCount() const { return m_directory ? static_cast<uint32>(m_directory->Size()) : 0; }
		Sptr<ShardExecutor> GetShard(uint32 index) const { return m_directory ? m_directory->ShardAt(index) : nullptr; }
		Sptr<ShardExecutor> GetShard(uint64 key) const { return m_directory ? m_directory->ShardAt(m_directory->PickShard(key)) : nullptr; }
		std::vector<Sptr<ShardExecutor>>& GetShards() { return m_directory->Shards(); }

		Sptr<ShardDirectory> GetDirectory() const { return m_directory; }

	private:
		void				WorkerLoop();
		void				TimerLoop();

	private:
		GlobalExecutorConfig									m_config;
		Atomic<bool>											m_running{ false };

		// offload (MPMC)
		moodycamel::BlockingConcurrentQueue<job::Job>			m_offload;

		// assist (MPMC)
		moodycamel::ConcurrentQueue<uint32>						m_assist;	// shard index

		// worker
		std::vector<std::thread>								m_workers;

		// timer
		std::thread												m_timerThread;

		struct TimedItem
		{
			uint64		due_ns; // (ns)
			job::Job	job;
		};
		struct TimedCmp
		{
			bool operator()(const TimedItem& a, const TimedItem& b)
			{
				return a.due_ns > b.due_ns;
			}
		};

		std::mutex												m_timerMutex;
		std::condition_variable									m_timerCv;
		std::priority_queue<TimedItem, std::vector<TimedItem>, TimedCmp>										m_timedItems;

		Sptr<ShardDirectory>									m_directory;
	};
}
