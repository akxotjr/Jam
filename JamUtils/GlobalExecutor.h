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
		// 0�̸� �ڵ� ����(����). �ڵ� ������ �� �ھ� ���� ���� ���� ���.
		uint32 workers = 0;

		// �ڵ� ���� �� ����
		uint32 minWorkers = 1;
		uint32 maxWorkers = UINT32_MAX;

		// �ڵ� Ʃ��(�ɼ�): ť ����/���� ������� ��Ÿ�� ������ �� ��� ����
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
