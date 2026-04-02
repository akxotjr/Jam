#include "pch.h"
#include "jamnet/core/executor/GlobalExecutor.h"
#include "jamnet/core/executor/ShardDirectory.h"
#include "jamnet/core/executor/ShardExecutor.h"
#include "jamnet/core/executor/ThreadRegistry.h"


namespace jam
{
	void GlobalExecutor::Init(const GlobalExecutorConfig& config)
	{
		m_config = config;
		m_config.layout = m_config.autoTune ? AutoLayout(m_config.layoutCfg) : m_config.layout;

		JAM_ASSERT(!m_config.layout.IsValid())

		ShardDirectoryConfig dirCfg = {
			.numShards	= m_config.layout.shards,
			.shardCfg	= m_config.shardCfg
		};

		m_directory = std::make_shared<ShardDirectory>(dirCfg);
		m_directory->Init();

		m_backend   = WinFiberBackend();
		m_scheduler = std::make_unique<FiberScheduler>(m_backend);
	}

	void GlobalExecutor::ShutDown()
	{
		Stop();
		Join();
	}

	void GlobalExecutor::Start()
	{
		if (m_running.exchange(true, std::memory_order_release))
			return;

		m_directory->Start();

		m_fiberThread = std::thread(&GlobalExecutor::FiberLoop, this);

		m_workers.reserve(m_config.layout.offload);
		for (int32 i = 0; i < m_config.layout.offload; ++i)
		{
			m_workers.emplace_back(&GlobalExecutor::WorkerLoop, this);
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

		m_offload.enqueue(Job([] {}));
	}

	void GlobalExecutor::Join()
	{
		m_directory->JoinAll();

		for (auto& t : m_workers)
			if (t.joinable()) t.join();

		m_workers.clear();

		if (m_fiberThread.joinable())
			m_fiberThread.join();

		m_scheduler.reset();
	}

	void GlobalExecutor::Submit(Job j)
	{
		m_offload.enqueue(std::move(j));
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
	}

	void GlobalExecutor::ConveyAll(Job j)
	{
		if (!m_directory) return;

		auto& shards = m_directory->Shards();
		const size_t n = shards.size();
		for (size_t i = 0; i < n; ++i)
		{
			auto& sh = shards[i];
			if (!sh) continue;
			if (i + 1 == n)
				sh->Submit(std::move(j));  
			else
				sh->Submit(j);        
		}
	}

	void GlobalExecutor::SpawnFiber(FiberFn fn, const FiberDesc& desc) const
	{
		if (m_scheduler) m_scheduler->PostSpawn(std::move(fn), desc);
	}

	void GlobalExecutor::ResumeFiber(FiberAwaitKey key) const
	{
		if (m_scheduler) m_scheduler->PostResume(key);
	}

	void GlobalExecutor::CancelFiberByKey(FiberAwaitKey key, eCancelCode code) const
	{
		if (m_scheduler) m_scheduler->PostCancelByKey(key, code);
	}

	void GlobalExecutor::CancelFiberById(uint32 id, eCancelCode code) const
	{
		if (m_scheduler) m_scheduler->PostCancelById(id, code);
	}

	GlobalExecutor::PeriodicHandle GlobalExecutor::ScheduleFixedRate(Job j, const PeriodicOptions& opt)
	{
		auto st = std::make_shared<PeriodicState>();
		st->period_ns = opt.period_ns;
		st->initialDelay_ns = opt.initialDelay_ns;
		st->maxCatchUp = opt.maxCatchUp;
		st->fixedRate = true;

		const uint32 id = m_periodicId.fetch_add(1, std::memory_order_relaxed);

		{
			WRITE_LOCK
			m_periodics[id] = st;
		}

		const char* fiberName = opt.name ? opt.name : "GE.PeriodicFixedRate";

		m_scheduler->PostSpawn(
			[this, st, j, id]
			{
				uint64 next_ns = NOW_NS() + st->initialDelay_ns;
				const uint64 period_ns = st->period_ns;

				while (m_running.load(std::memory_order_acquire) && !st->cancelled.load(std::memory_order_relaxed))
				{
					m_scheduler->SleepUntil(next_ns);
					if (!m_running.load(std::memory_order_acquire) || st->cancelled.load(std::memory_order_relaxed))
						break;

					Submit(j);

					next_ns += period_ns;

					int32 catches = 0;
					for (uint64 now_ns = NOW_NS(); now_ns >= next_ns && catches < st->maxCatchUp; now_ns = NOW_NS(), ++catches)
					{
						if (!m_running.load(std::memory_order_acquire) || st->cancelled.load(std::memory_order_relaxed))
							break;
						Submit(j);
						next_ns += period_ns;
					}
				}

				WRITE_LOCK
				m_periodics.erase(id);
			},
			FiberDesc{ .name = fiberName }
		);

		return PeriodicHandle{ id };
	}

	GlobalExecutor::PeriodicHandle GlobalExecutor::ScheduleFixedDelay(Job j, const PeriodicOptions& opt)
	{
		auto st = std::make_shared<PeriodicState>();
		st->period_ns = opt.period_ns;
		st->initialDelay_ns = opt.initialDelay_ns;
		st->maxCatchUp = 0;
		st->fixedRate = false;

		const uint32 id = m_periodicId.fetch_add(1, std::memory_order_relaxed);

		{
			WRITE_LOCK
			m_periodics[id] = st;
		}

		const char* fiberName = opt.name ? opt.name : "GE.PeriodicFD";

		m_scheduler->PostSpawn(
			[this, st, j, id]()
			{
				// 초기 지연
				uint64 next = NOW_NS() + st->initialDelay_ns;
				m_scheduler->SleepUntil(next);

				while (m_running.load(std::memory_order_acquire) && !st->cancelled.load(std::memory_order_relaxed))
				{
					// 1) 실행
					Submit(j);
					if (!m_running.load(std::memory_order_acquire) || st->cancelled.load(std::memory_order_relaxed))
						break;

					// 2) 고정 딜레이
					m_scheduler->SleepUntil(NOW_NS() + st->period_ns);
				}

				// 정리
				WRITE_LOCK
				m_periodics.erase(id);
			},
			FiberDesc{ .name = fiberName }
		);

		return PeriodicHandle{ id };
	}

	bool GlobalExecutor::CancelPerioidc(PeriodicHandle h)
	{
		WRITE_LOCK
		auto it = m_periodics.find(h.id);
		if (it == m_periodics.end()) return false;
		if (auto st = it->second.lock())
		{
			st->cancelled.store(true, std::memory_order_relaxed);
			return true;
		}
		return false;
	}

	void GlobalExecutor::WorkerLoop()
	{
		static std::atomic<uint32> workerIndex{ 0 };
		uint32	myIndex = workerIndex.fetch_add(1, std::memory_order_relaxed);
		std::string threadName = std::format("GlobalExecutor#Worker{}", myIndex);
		ThreadRegistry::InitExecutorThread(threadName, this);

		while (m_running.load())
		{
           m_metricWorkerLoopCount.fetch_add(1, std::memory_order_relaxed);
			Job j([] {});
         const uint64 waitStart_ns = NOW_NS();
			const bool dequeued = m_offload.wait_dequeue_timed(j, 1);
			m_metricWorkerWaitCost_ns.fetch_add(NOW_NS() - waitStart_ns, std::memory_order_relaxed);

			if (dequeued)
			{
				const uint64 execStart_ns = NOW_NS();
				j.Execute();
               m_metricWorkerJobExecCount.fetch_add(1, std::memory_order_relaxed);
				m_metricWorkerJobExecCost_ns.fetch_add(NOW_NS() - execStart_ns, std::memory_order_relaxed);
			}
			else
			{
				m_metricWorkerIdleLoopCount.fetch_add(1, std::memory_order_relaxed);
			}
		}
	}

	void GlobalExecutor::FiberLoop()
	{
		ThreadRegistry::InitExecutorThread("GlobalExecutor#Fiber", this);

		m_scheduler->AttachToCurrentThread();

		while (m_running.load(std::memory_order_acquire))
		{
            m_metricFiberLoopCount.fetch_add(1, std::memory_order_relaxed);

			const uint64 pollStart_ns = NOW_NS();
			m_scheduler->Poll(64, pollStart_ns);
			const uint64 pollCost_ns = NOW_NS() - pollStart_ns;
			m_metricFiberPollCount.fetch_add(1, std::memory_order_relaxed);
			m_metricFiberPollCost_ns.fetch_add(pollCost_ns, std::memory_order_relaxed);

			const auto& profile = m_scheduler->Profile();
			if (profile.lastPollReadyRunCount == 0)
				m_metricFiberEmptyPollCount.fetch_add(1, std::memory_order_relaxed);
			m_metricFiberReadyRunCount.store(profile.readyRunCount, std::memory_order_relaxed);

			const uint64 idleStart_ns = NOW_NS();
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
           m_metricFiberSleepCost_ns.fetch_add(NOW_NS() - idleStart_ns, std::memory_order_relaxed);
		}

		m_scheduler->DetachFromThread();
	}

	GlobalExecutorMetricsSnapshot GlobalExecutor::GetMetricsSnapshot() const
	{
		GlobalExecutorMetricsSnapshot s{};
		s.workerLoopCount		= m_metricWorkerLoopCount.load(std::memory_order_relaxed);
		s.workerJobExecCount	= m_metricWorkerJobExecCount.load(std::memory_order_relaxed);
		s.workerIdleLoopCount	= m_metricWorkerIdleLoopCount.load(std::memory_order_relaxed);
		s.workerWaitCost_ns		= m_metricWorkerWaitCost_ns.load(std::memory_order_relaxed);
		s.workerJobExecCost_ns	= m_metricWorkerJobExecCost_ns.load(std::memory_order_relaxed);

		s.fiberLoopCount		= m_metricFiberLoopCount.load(std::memory_order_relaxed);
		s.fiberPollCount		= m_metricFiberPollCount.load(std::memory_order_relaxed);
		s.fiberEmptyPollCount	= m_metricFiberEmptyPollCount.load(std::memory_order_relaxed);
		s.fiberPollCost_ns		= m_metricFiberPollCost_ns.load(std::memory_order_relaxed);
		s.fiberSleepCost_ns		= m_metricFiberSleepCost_ns.load(std::memory_order_relaxed);
		s.fiberReadyRunCount	= m_metricFiberReadyRunCount.load(std::memory_order_relaxed);
		return s;
	}

	std::vector<ShardExecutorMetricsSnapshot> GlobalExecutor::GetShardMetricsSnapshots() const
	{
		std::vector<ShardExecutorMetricsSnapshot> snapshots;
		if (!m_directory)
			return snapshots;

		auto& shards = m_directory->Shards();
		snapshots.reserve(shards.size());
		for (auto& shard : shards)
		{
			if (!shard)
				continue;
			snapshots.push_back(shard->GetMetricsSnapshot());
		}

		return snapshots;
	}

	void GlobalExecutor::ResetMetrics()
	{
		m_metricWorkerLoopCount.store(0, std::memory_order_relaxed);
		m_metricWorkerJobExecCount.store(0, std::memory_order_relaxed);
		m_metricWorkerIdleLoopCount.store(0, std::memory_order_relaxed);
		m_metricWorkerWaitCost_ns.store(0, std::memory_order_relaxed);
		m_metricWorkerJobExecCost_ns.store(0, std::memory_order_relaxed);

		m_metricFiberLoopCount.store(0, std::memory_order_relaxed);
		m_metricFiberPollCount.store(0, std::memory_order_relaxed);
		m_metricFiberEmptyPollCount.store(0, std::memory_order_relaxed);
		m_metricFiberPollCost_ns.store(0, std::memory_order_relaxed);
		m_metricFiberSleepCost_ns.store(0, std::memory_order_relaxed);
		m_metricFiberReadyRunCount.store(0, std::memory_order_relaxed);

		if (m_scheduler)
			m_scheduler->ResetProfile();

		if (m_directory)
		{
			for (auto& shard : m_directory->Shards())
			{
				if (shard)
					shard->ResetMetrics();
			}
		}
	}
}
