#include "pch.h"
#include "GlobalExecutor.h"
#include "Clock.h"
#include "ShardExecutor.h"
#include "ShardEndpoint.h"


namespace jam::utils::exec
{
	GlobalExecutor::GlobalExecutor(const GlobalExecutorConfig& config)
		: m_config(config)
	{
	}

	GlobalExecutor::~GlobalExecutor()
	{
		Stop();
		Join();
	}

	void GlobalExecutor::Start(const xvector<Sptr<ShardExecutor>>& shards)
	{
		if (m_running.exchange(true))
			return;

		m_shards = shards;

		const uint32 workersToStart = ComputeWorkerCount(m_shards.size());
		m_workers.reserve(workersToStart);
		for (int32 i = 0; i < workersToStart; ++i)
			m_workers.emplace_back(&GlobalExecutor::WorkerLoop, this);

		m_timerThread = std::thread(&GlobalExecutor::TimerLoop, this);
	}

	void GlobalExecutor::Stop()
	{
		if (!m_running.exchange(false))
			return;

		m_offload.enqueue(job::Job([] {}));
		m_timerCv.notify_all();
		m_assist.enqueue(UINT32_MAX);
	}

	void GlobalExecutor::Join()
	{
		for (auto& t : m_workers)
			if (t.joinable()) t.join();

		m_workers.clear();

		if (m_timerThread.joinable())
			m_timerThread.join();
	}

	void GlobalExecutor::Post(job::Job job)
	{
		m_offload.enqueue(std::move(job));
	}

	void GlobalExecutor::PostAfter(job::Job job, uint64 delay_ns)
	{
		std::unique_lock lk(m_timerMutex);
		const uint64 now_ns = Clock::Instance().NowNs();
		m_timedItems.push_back(TimedItem{ now_ns + delay_ns, std::move(job) });
		std::push_heap(m_timedItems.begin(), m_timedItems.end(), std::greater<>{});
		m_timerCv.notify_one();
	}

	void GlobalExecutor::RequestAssist(uint32 shardIndex)
	{
		m_assist.enqueue(shardIndex);
	}

	std::shared_ptr<ShardExecutor> GlobalExecutor::GetShard(uint32 index) const
	{
		if (m_shards.empty()) return nullptr;
		return m_shards[index % m_shards.size()];
	}

	void GlobalExecutor::WorkerLoop()
	{
		while (m_running.load())
		{
			// 1) 오프로딩
			job::Job j([] {});
			if (m_offload.wait_dequeue_timed(j, 1))
				j.Execute();

			// 2) Assist 요청 처리
			uint32 shardIdx = UINT32_MAX;
			while (m_assist.try_dequeue(shardIdx))
			{
				if (!m_running.load() || shardIdx == UINT32_MAX)
					break;
				if (auto shard = GetShard(shardIdx))
				{
					// 짧게 한 번만 보조
					shard->AssistDrainOnce(/*maxMailboxes*/ 16, /*budgetPerMailbox*/ 16);
				}
			}
		}
	}

	void GlobalExecutor::TimerLoop()
	{
		std::unique_lock lk(m_timerMutex);
		while (m_running.load())
		{
			if (m_timedItems.empty())
			{
				m_timerCv.wait_for(lk, std::chrono::milliseconds(10));
				continue;
			}

			std::pop_heap(m_timedItems.begin(), m_timedItems.end(), std::greater<>{});
			TimedItem top = std::move(m_timedItems.back());
			m_timedItems.pop_back();

			const uint64 now_ns = Clock::Instance().NowNs();
			if (top.due_ns > now_ns)
				m_timerCv.wait_for(lk, std::chrono::milliseconds(top.due_ns - now_ns));

			lk.unlock();
			Post(std::move(top.job));
			lk.lock();
		}
	}

	uint32 GlobalExecutor::ComputeWorkerCount(uint32 shardCount) const
	{
		uint32 H = max(1, std::thread::hardware_concurrency());
		uint32 S = shardCount;
		uint32 spare = (H > S) ? (H - S) : 1;

		uint32 desired = (m_config.workers == 0) ? spare : m_config.workers;
		
		if (m_config.minWorkers > 0)
			desired = max(desired, m_config.minWorkers);
		if (m_config.maxWorkers != UINT32_MAX)
			desired = min(desired, m_config.maxWorkers);
		return max(1, desired);
	}
}
