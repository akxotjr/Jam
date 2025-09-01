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
			// 1) �����ε�
			job::Job j([] {});
			if (m_offload.wait_dequeue_timed(j, 1))
				j.Execute();

			// 2) Assist ��û ó��
			uint32 shardIdx = UINT32_MAX;
			while (m_assist.try_dequeue(shardIdx))
			{
				if (!m_running.load() || shardIdx == UINT32_MAX)
					break;
				if (auto shard = GetShard(shardIdx))
				{
					// ª�� �� ���� ����
					shard->AssistDrainOnce(/*maxMailboxes*/ 16, /*budgetPerMailbox*/ 16);
				}
			}
		}
	}

	void GlobalExecutor::TimerLoop()
	{
		using namespace std::chrono; // ���� (steady_clock, nanoseconds)

		std::unique_lock lk(m_timerMutex);

		while (m_running.load())
		{
			// ������ ���� ������ (�Ǵ� �������) ���
			m_timerCv.wait(lk, [this] { return !m_running.load() || !m_timedItems.empty(); });
			if (!m_running.load()) break;
			if (m_timedItems.empty()) continue;

			// ���� ���� �̸� Ÿ�̸� ���� (pq: top�� ���� �̸� due���� ��)
			TimedItem top = m_timedItems.top();
			m_timedItems.pop();

			const uint64 target_ns = top.due_ns;
			bool reschedule = false;

			while (m_running.load())
			{
				const uint64 now_ns = Clock::Instance().NowNs();
				if (now_ns >= target_ns)
					break; // ���� �ð� ����

				const uint64 wait_ns = target_ns - now_ns;
				const auto   deadline = steady_clock::now() + nanoseconds(wait_ns);

				// ���� or "�� �̸� due"�� �����ϸ� ���
				const bool woke = m_timerCv.wait_until(lk, deadline, [this, target_ns] {
					if (!m_running.load()) return true;
					// pq�� ���� �ʰ�, ���� ��� �ִ� target���� �� �̸� due�� ����°�?
					return !m_timedItems.empty() && (m_timedItems.top().due_ns < target_ns);
					});

				if (!m_running.load()) break;

				// �� �̸� Ÿ�̸Ӱ� �����ߴٸ�, ���� ��� �ִ� top�� �ǵ����� �缱��
				if (woke && !m_timedItems.empty() && m_timedItems.top().due_ns < target_ns) {
					m_timedItems.push(std::move(top)); // �ǵ�����
					reschedule = true;
					break; // �ٱ� ������ ���� �� top�� �̴´�
				}

				// ���۸�� ����ũ���̸� while�� ���ư� ���� �ð��� �ٽ� ���
			}

			if (!m_running.load()) break;

			if (reschedule) {
				// �� �̸� �׸��� ������, �ٱ� �������� �ٽ� top�� ����
				continue;
			}

			// ���� ������ �ð� �� �� Ǯ�� ����(�Ǵ� ��Ŀ ť�� �����ε�)
			lk.unlock();
			Post(std::move(top.job)); // TODO: �۷ι� ��Ŀ vs ���� �� ����
			lk.lock();
		}
	}

}
