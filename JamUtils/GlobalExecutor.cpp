#include "pch.h"
#include "GlobalExecutor.h"
#include "Clock.h"
#include "ShardDirectory.h"
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

	void GlobalExecutor::Init(const std::vector<Sptr<ShardExecutor>>& shards)
	{
		m_config.layout = sys::ResolveCoreLayout(m_config.layout, m_config.layoutCfg);

		ShardDirectoryConfig dirCfg = {
			.ownership = shards.empty() ? eShardOwnership::OWN : eShardOwnership::ADOPT,
			.numShards = m_config.layout.shards,
			.shardCfg = m_config.shardCfg
		};

		m_directory = std::make_shared<ShardDirectory>(dirCfg, weak_from_this());
		m_directory->Init(shards);
	}

	void GlobalExecutor::Start()
	{
		if (m_running.exchange(true, std::memory_order_release))
			return;

		m_directory->Start();

		m_workers.reserve(m_config.layout.io);
		for (int32 i = 0; i < m_config.layout.io; ++i)
			m_workers.emplace_back(&GlobalExecutor::WorkerLoop, this);

		if (m_config.layout.timers > 0)
			m_timerThread = std::thread(&GlobalExecutor::TimerLoop, this);
	}

	void GlobalExecutor::Stop()
	{
		if (!m_running.exchange(false))
			return;

		m_directory->StopAll();

		m_offload.enqueue(job::Job([] {}));
		m_timerCv.notify_all();
		m_assist.enqueue(UINT32_MAX);
	}

	void GlobalExecutor::Join()
	{
		m_directory->JoinAll();

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
		m_timedItems.push(TimedItem{ .due_ns= now_ns + delay_ns, .job= std::move(job) });
		m_timerCv.notify_one();
	}

	void GlobalExecutor::RequestAssist(uint32 shardIndex)
	{
		m_assist.enqueue(shardIndex);
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
		using namespace std::chrono; // 편의 (steady_clock, nanoseconds)

		std::unique_lock lk(m_timerMutex);

		while (m_running.load())
		{
			// 아이템 생길 때까지 (또는 종료까지) 대기
			m_timerCv.wait(lk, [this] { return !m_running.load() || !m_timedItems.empty(); });
			if (!m_running.load()) break;
			if (m_timedItems.empty()) continue;

			// 현재 가장 이른 타이머 선정 (pq: top이 가장 이른 due여야 함)
			TimedItem top = m_timedItems.top();
			m_timedItems.pop();

			const uint64 target_ns = top.due_ns;
			bool reschedule = false;

			while (m_running.load())
			{
				const uint64 now_ns = Clock::Instance().NowNs();
				if (now_ns >= target_ns)
					break; // 만료 시각 도달

				const uint64 wait_ns = target_ns - now_ns;
				const auto   deadline = steady_clock::now() + nanoseconds(wait_ns);

				// 종료 or "더 이른 due"가 도착하면 깨어남
				const bool woke = m_timerCv.wait_until(lk, deadline, [this, target_ns] {
					if (!m_running.load()) return true;
					// pq가 비지 않고, 현재 잡고 있는 target보다 더 이른 due가 생겼는가?
					return !m_timedItems.empty() && (m_timedItems.top().due_ns < target_ns);
					});

				if (!m_running.load()) break;

				// 더 이른 타이머가 도착했다면, 지금 들고 있던 top을 되돌리고 재선택
				if (woke && !m_timedItems.empty() && m_timedItems.top().due_ns < target_ns) {
					m_timedItems.push(std::move(top)); // 되돌리기
					reschedule = true;
					break; // 바깥 루프로 나가 새 top을 뽑는다
				}

				// 스퍼리어스 웨이크업이면 while로 돌아가 남은 시간을 다시 계산
			}

			if (!m_running.load()) break;

			if (reschedule) {
				// 더 이른 항목이 있으니, 바깥 루프에서 다시 top을 뽑자
				continue;
			}

			// 이제 실행할 시간 → 락 풀고 실행(또는 워커 큐로 오프로드)
			lk.unlock();
			Post(std::move(top.job)); // TODO: 글로벌 워커 vs 샤드 중 선택
			lk.lock();
		}
	}

}
