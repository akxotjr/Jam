

// Prevent windows.h from pulling in winsock.h and defining min/max macros
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

// Winsock2 must be included before windows.h
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#pragma comment(lib, "ws2_32.lib")

// platform
#include <windows.h>
#include <winnt.h>         //  SLIST_ENTRY, SLIST_HEADER
#include <intrin.h>        //  InterlockedPushEntrySList

// std
#include <iostream>
#include <algorithm>
#include <vector>
#include <array>
#include <queue>
#include <stack>
#include <map>
#include <unordered_map>
#include <set>
#include <unordered_set>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <type_traits>
#include <mutex>
#include <atomic>
#include <thread>
#include <string>
#include <string_view>
#include <format>
#include <random>
#include <conio.h>
#include <fstream>
#include <filesystem>
#include <numeric>

// 3rd party
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <entt/entt.hpp>
#include <concurrentqueue/concurrentqueue.h>
#include <concurrentqueue/blockingconcurrentqueue.h>
#include <flatbuffers/flatbuffers.h>
#include <physx/PxPhysicsAPI.h>


#include <JamNetAPI.h>

using namespace std;
using namespace jam;

namespace
{
	struct JobTrace
	{
		uint64			enqueueTs	= 0;
		uint64			startTs		= 0;
		uint64			endTs		= 0;
		uint64			lockWaitNs	= 0;
		bool			isWaitJob	= false;
	};

	struct QueueSample
	{
		uint64			timestamp_ns		= 0;
		size_t			geQ					= 0;
		size_t			shardMaxQ			= 0;
		size_t			shardTotalQ			= 0;
	};

	enum class SyncMode { ShardSerial, TwoStageLock };
	enum class WaitMode { None, ThreadBlock, FiberSuspend };

	struct ScenarioSpec
	{
		std::string		name;
		int32			duration_sec		= 60;
		int32			submitInterval_ms	= 1;
		int32			geJobsPerTick		= 0;
		int32			shardJobsPerTick	= 0;
		int32			mailboxJobsPerTick	= 0;
		bool			burst				= false;
		int32			costClass			= 0;		// 0=Light, 1=Medium, 2=Heavy, 3=Mix
		bool			memoryTouch			= false;
		bool			shardSkew			= false;
		bool			ownerContention		= false;
		int32			ownerPoolSize		= 1024;
		int32			hotOwnerPoolSize	= 16;
		int32			waitEveryN			= 0;
		bool			useFiberPath		= false;
		SyncMode		syncMode			= SyncMode::ShardSerial;
		WaitMode		waitMode			= WaitMode::None;
	};

	std::mutex g_globalMutex;
	std::vector<std::mutex> g_ownerMutexes(2048);


	// Thread-local memory touch buffer
	constexpr size_t MEMORY_BUFFER_SIZE = 256 * 1024;

	struct alignas(64) ThreadLocalMemoryBuffer
	{
		std::array<uint8_t, MEMORY_BUFFER_SIZE> data{};
	};

	thread_local ThreadLocalMemoryBuffer g_tlsMemoryBuffer;


	struct BurstTraceRow
	{
		uint64			timeBucket_ms			= 0;
		uint64			completedJobs			= 0;
		double			p99Latency_us			= 0.0;
		size_t			queueDepthMax			= 0;
	};

	struct ScenarioRow
	{
		std::string		scenario;
		int32			duration_sec			= 0;
		double			avgLatency_us			= 0.0;
		double			p50Latency_us			= 0.0;
		double			p95Latency_us			= 0.0;
		double			p99Latency_us			= 0.0;
		double			maxLatency_us			= 0.0;
		double			avgQueueWait_us			= 0.0;
		double			p99QueueWait_us			= 0.0;
		double			avgQueueDepth			= 0.0;
		double			maxQueueDepth			= 0.0;
		double			producerRateJobsPerS	= 0.0;
		double			completedJobsPerS		= 0.0;
		double			shardMaxQueueDepth		= 0.0;
		double			shardStddevThroughput	= 0.0;
		double			workerBusyPct			= 0.0;
		double			backlogRecoveryTime_ms	= 0.0;

		double			geJobsPerSec			= 0.0;
		double			geIdlePct				= 0.0;
		double			geFiberAvgPoll_us		= 0.0;
		double			shardAvgExecJobsPerSec	= 0.0;
		double			shardFiberRunsPerSec	= 0.0;
		double			shardIdlePct			= 0.0;
		double			mailboxJobsPerSec		= 0.0;
		double			tickCatchUpPerSec		= 0.0;

		double			avgLockWait_us			= 0.0;
		double			p99LockWait_us			= 0.0;
		double			contendedLockPct		= 0.0;
		double			runnableP99_us			= 0.0;
		double			waitP99_us				= 0.0;
		double			hotOwnerHitPct			= 0.0;


		bool						hasBurstTrace = false;
		std::vector<BurstTraceRow>	burstTraceRows;
	};




	uint64 SelectOwnerKey(const ScenarioSpec& spec, uint64 seq)
	{
		uint64 state = seq * 0x9E3779B97F4A7C15ULL + 0xBF58476D1CE4E5B9ULL;
		state ^= (state >> 30);
		state *= 0xBF58476D1CE4E5B9ULL;
		state ^= (state >> 27);
		state *= 0x94D049BB133111EBULL;
		state ^= (state >> 31);

		const uint64 ownerPool = static_cast<uint64>(std::max(1, spec.ownerPoolSize));
		if (!spec.ownerContention)
			return state % ownerPool;

		const uint64 hotPool = static_cast<uint64>(std::clamp(spec.hotOwnerPoolSize, 1, spec.ownerPoolSize));
		if (hotPool >= ownerPool)
			return state % ownerPool;

		if ((state % 100) < 80)
			return state % hotPool;

		const uint64 coldPool = ownerPool - hotPool;
		return hotPool + (state % coldPool);
	}

	void SimulateJobCost(int32 costClass, bool memoryTouch, bool applyWait, WaitMode waitMode)
	{
		if (costClass != 0)
		{
			const uint64 start_ns = NOW_NS();
			const uint64 targetDelay_ns = (costClass == 1) ? 5_us : 50_us;

			if (memoryTouch)
			{
				uint64 seed = start_ns ^ (targetDelay_ns << 1);
				auto& localBuf = g_tlsMemoryBuffer.data;

				size_t idx = static_cast<size_t>(seed % MEMORY_BUFFER_SIZE);
				int32 stepCount = 0;

				while (NOW_NS() - start_ns < targetDelay_ns)
				{
					localBuf[idx]++;

					if ((stepCount++ & 7) == 0)
					{
						seed = seed * 6364136223846793005ULL + 1ULL;
						const size_t jitter = static_cast<size_t>(((seed >> 9) & 0x3) * 64);
						idx = (idx + 64 + jitter) % MEMORY_BUFFER_SIZE;
					}
					else
					{
						idx = (idx + 64) % MEMORY_BUFFER_SIZE;
					}
#if defined(_M_IX86) || defined(_M_X64)
					_mm_pause();
#endif
				}
			}
			else
			{
				while (NOW_NS() - start_ns < targetDelay_ns)
				{
#if defined(_M_IX86) || defined(_M_X64)
					_mm_pause();
#endif
				}
			}
		}

		if (!applyWait)
			return;

		if (waitMode == WaitMode::ThreadBlock)
		{
			std::this_thread::sleep_for(std::chrono::microseconds(50));
		}
		else if (waitMode == WaitMode::FiberSuspend)
		{
			if (ShardTLS::IsShardThread())
				ShardTLS::GetCurrentChecked().scheduler->SleepUntil(NOW_NS() + 50_us);
			else
				std::this_thread::sleep_for(std::chrono::microseconds(50));
		}
	}
	void PrintMetricsSnapshotToLog()
	{
		auto ge = GLOBAL_EXEC.GetMetricsSnapshot();
		const double geIdleRatio  = ge.workerLoopCount    == 0 ? 0.0 : (static_cast<double>(ge.workerIdleLoopCount)  / static_cast<double>(ge.workerLoopCount));
		const double geAvgWait_us = ge.workerLoopCount    == 0 ? 0.0 : (static_cast<double>(ge.workerWaitCost_ns)    / static_cast<double>(ge.workerLoopCount) / 1000.0);
		const double geAvgJob_us  = ge.workerJobExecCount == 0 ? 0.0 : (static_cast<double>(ge.workerJobExecCost_ns) / static_cast<double>(ge.workerJobExecCount) / 1000.0);

		JAMNET_LOG_INFO("[GE] loops={}, jobs={}, idleLoops={}, idleRatio={:.2f}%, avgWait={:.3f}us, avgJob={:.3f}us", ge.workerLoopCount, ge.workerJobExecCount, ge.workerIdleLoopCount, geIdleRatio * 100.0, geAvgWait_us, geAvgJob_us);

		const double fiberEmptyRatio = ge.fiberPollCount == 0 ? 0.0 : (static_cast<double>(ge.fiberEmptyPollCount) / static_cast<double>(ge.fiberPollCount));
		const double fiberAvgPoll_us = ge.fiberPollCount == 0 ? 0.0 : (static_cast<double>(ge.fiberPollCost_ns)	   / static_cast<double>(ge.fiberPollCount) / 1000.0);
		JAMNET_LOG_INFO("[GE/Fiber] polls={}, emptyPolls={}, emptyRatio={:.2f}%, readyRuns={}, avgPoll={:.3f}us", ge.fiberPollCount, ge.fiberEmptyPollCount, fiberEmptyRatio * 100.0, ge.fiberReadyRunCount, fiberAvgPoll_us);

		for (const auto& shard : GLOBAL_EXEC.GetShardMetricsSnapshots())
		{
			const double loopIdleRatio = shard.loopCount == 0 ? 0.0 : (static_cast<double>(shard.idleLoopCount) / static_cast<double>(shard.loopCount));
			const double avgPoll_us	   = shard.schedulerPollCount == 0 ? 0.0 : (static_cast<double>(shard.schedulerPollCost_ns) / static_cast<double>(shard.schedulerPollCount) / 1000.0);
			JAMNET_LOG_INFO("[Shard#{}] loops={}, idleRatio={:.2f}%, ingressJobs={}, execJobs={}, mailboxJobs={}, schedulerPolls={}, avgPoll={:.3f}us, tick={}, catchUp={}",
				shard.shardIndex,
				shard.loopCount,
				loopIdleRatio * 100.0,
				shard.ingressJobCount,
				shard.processJobsExecCount,
				shard.mailboxJobMoveCount,
				shard.schedulerPollCount,
				avgPoll_us,
				shard.tickCount,
				shard.tickCatchUpCount);
		}
	}

	bool WaitForScenarioDrain(
		const std::vector<std::shared_ptr<ShardExecutor>>& shards,
		const std::atomic<uint64_t>& totalSubmitted,
		const std::atomic<uint64_t>& totalCompleted,
		uint64 timeoutNs)
	{
		const uint64 begin = NOW_NS();

		for (;;)
		{
			const uint64 submitted = totalSubmitted.load(std::memory_order_acquire);
			const uint64 completed = totalCompleted.load(std::memory_order_acquire);

			size_t geQ = GLOBAL_EXEC.GetQueueSize();
			size_t shardQ = 0;
			for (const auto& sh : shards)
				shardQ += sh->GetQueueSize();

			if (completed >= submitted && geQ == 0 && shardQ == 0)
				return true;

			if (NOW_NS() - begin >= timeoutNs)
				return false;

			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}

	ScenarioRow RunScenario(const ScenarioSpec& spec)
	{
		GLOBAL_EXEC.ResetMetrics();

		std::vector<std::shared_ptr<ShardExecutor>> shards;
		for (uint32 i = 0; i < GLOBAL_EXEC.GetShardCount(); ++i)
		{
			if (auto sh = GLOBAL_EXEC.GetShard(i))
				shards.push_back(std::move(sh));
		}

		std::vector<std::shared_ptr<Mailbox>> mailboxes;
		mailboxes.reserve(shards.size());
		for (auto& sh : shards)
		{
			mailboxes.push_back(sh->CreateMailbox());
		}

		// Allocate sufficient trace buffer: (Jobs * ticks * seconds) * capacity factor
		const size_t maxTraces = static_cast<size_t>(
			(spec.geJobsPerTick + (spec.shardJobsPerTick + spec.mailboxJobsPerTick) * std::max<size_t>(1, shards.size()))
			* (1000 / spec.submitInterval_ms) * spec.duration_sec * (spec.burst ? 5 : 2)
			);
		std::vector<JobTrace> traces(maxTraces);
		std::atomic<size_t> traceIdx{ 0 };
		const uint64 warmupEndTs = NOW_NS() + 5_s; // 5초 웜업

		std::vector<QueueSample> queueSamples;
		queueSamples.reserve(spec.duration_sec * 100); // 10ms 마다 1개씩 1초에 100개
		std::mutex qMutex;

		std::atomic<bool> monitorRunning{ true };
		std::thread monitor([&]() {
			while (monitorRunning.load(std::memory_order_relaxed))
			{
				const uint64 now = NOW_NS();
				if (now > warmupEndTs)
				{
					size_t qGe = GLOBAL_EXEC.GetQueueSize();
					size_t sqMax = 0, sqTotal = 0;
					for (auto& sh : shards)
					{
						size_t sq = sh->GetQueueSize();
						sqTotal += sq;
						sqMax = std::max(sq, sqMax);
					}
					std::scoped_lock lock(qMutex);
					queueSamples.push_back({ .timestamp_ns = now, .geQ = qGe, .shardMaxQ = sqMax, .shardTotalQ = sqTotal });
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}
			});

		std::atomic<uint64> totalProduced{ 0 };
		std::atomic<uint64> totalSubmitted{ 0 };
		std::atomic<uint64> totalCompleted{ 0 };
		std::atomic<uint64> submitSeq{ 0 };
		std::atomic<uint64> hotOwnerHitCount{ 0 };

		auto dispatchBenchJob = [&](auto submitFn, uint64 ownerKey, bool isWaitJob, int32 baseCost, bool memTouch, SyncMode sync, WaitMode wait)
			{
				totalProduced.fetch_add(1, std::memory_order_relaxed);
				totalSubmitted.fetch_add(1, std::memory_order_relaxed);
				const uint64 enq = NOW_NS();
				if (spec.ownerContention && ownerKey < static_cast<uint64>(std::max(1, spec.hotOwnerPoolSize)))
					hotOwnerHitCount.fetch_add(1, std::memory_order_relaxed);

				int32 actualCost = baseCost;
				if (baseCost == 3)
				{
					const uint32 r = static_cast<uint32>(enq % 100);
					if (r < 50)		 actualCost = 0;
					else if (r < 80) actualCost = 1;
					else			 actualCost = 2;
				}

				auto fn = [enq, ownerKey, isWaitJob, actualCost, memTouch, sync, wait, warmupEndTs, &totalCompleted, &traces, &traceIdx, maxTraces]() mutable {
					const uint64 start = NOW_NS();
					uint64 lockWaitNs = 0;

                    if (sync == SyncMode::TwoStageLock)
					{
						const uint64 lockStart = NOW_NS();
						{
							std::scoped_lock globalGuard(g_globalMutex);
						}
						auto& ownerMutex = g_ownerMutexes[ownerKey % g_ownerMutexes.size()];
						std::scoped_lock ownerGuard(ownerMutex);
						lockWaitNs = NOW_NS() - lockStart;
						SimulateJobCost(actualCost, memTouch, isWaitJob, wait);
					}
					else
					{
                        SimulateJobCost(actualCost, memTouch, isWaitJob, wait);
					}

					const uint64 end = NOW_NS();

					totalCompleted.fetch_add(1, std::memory_order_release);

					// 웜업 구간 제외
					if (enq > warmupEndTs)
					{
						size_t idx = traceIdx.fetch_add(1, std::memory_order_relaxed);
						if (idx < maxTraces)
						{
							traces[idx] = {
								.enqueueTs	= enq,
								.startTs	= start,
								.endTs		= end,
								.lockWaitNs = lockWaitNs,
								.isWaitJob	= isWaitJob
							};
						}
					}
				};

				submitFn(std::move(fn));
			};

		std::atomic<bool> produce{ true };
		std::thread producer([&]()
			{
				const auto start = std::chrono::steady_clock::now();
				while (produce.load(std::memory_order_acquire))
				{
					int32 geJobs	  = spec.geJobsPerTick;
					int32 shardJobs   = spec.shardJobsPerTick;
					int32 mailboxJobs = spec.mailboxJobsPerTick;

					if (spec.burst)
					{
						const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
						if (((elapsedMs / 500) % 2) == 0)
						{
							geJobs		*= 4;
							shardJobs	*= 4;
							mailboxJobs *= 4;
						}
						else
						{
							geJobs		= std::max<int32>(1, geJobs / 2);
							shardJobs	= std::max<int32>(1, shardJobs / 2);
							mailboxJobs = std::max<int32>(1, mailboxJobs / 2);
						}
					}

					auto submitToShard = [&](std::shared_ptr<jam::ShardExecutor>& sh, uint64 ownerKey, bool isWaitJob)
					{
						if (spec.useFiberPath)
							dispatchBenchJob([&](auto fn) { sh->SpawnFiber(std::move(fn), {}); }, ownerKey, isWaitJob, spec.costClass, spec.memoryTouch, spec.syncMode, spec.waitMode);
						else
							dispatchBenchJob([&](auto fn) { sh->Submit(Job(std::move(fn))); }, ownerKey, isWaitJob, spec.costClass, spec.memoryTouch, spec.syncMode, spec.waitMode);
					};

					for (int32 i = 0; i < geJobs; ++i)
					{
						const uint64 seq	   = submitSeq.fetch_add(1, std::memory_order_relaxed);
						const uint64 ownerKey  = SelectOwnerKey(spec, seq);
						const bool   isWaitJob = spec.waitEveryN > 0 && (((seq + 1) % static_cast<uint64>(spec.waitEveryN)) == 0);
						
						if (spec.useFiberPath)
							dispatchBenchJob([&](auto fn) { GLOBAL_EXEC.SpawnFiber(std::move(fn)); }, ownerKey, isWaitJob, spec.costClass, spec.memoryTouch, spec.syncMode, spec.waitMode);
						else
							dispatchBenchJob([&](auto fn) { GLOBAL_EXEC.Submit(Job(std::move(fn))); }, ownerKey, isWaitJob, spec.costClass, spec.memoryTouch, spec.syncMode, spec.waitMode);
					}

					const int32 totalShardJobs = shardJobs * static_cast<int32>(std::max<size_t>(1, shards.size()));
					for (int32 i = 0; i < totalShardJobs; ++i)
					{
						const uint64 seq	   = submitSeq.fetch_add(1, std::memory_order_relaxed);
						const uint64 ownerKey  = SelectOwnerKey(spec, seq);
						const bool   isWaitJob = spec.waitEveryN > 0 && (((seq + 1) % static_cast<uint64>(spec.waitEveryN)) == 0);
						
						size_t targetIdx = static_cast<size_t>(ownerKey % std::max<size_t>(1, shards.size()));
						if (spec.shardSkew && shards.size() > 1 && !spec.ownerContention && (seq % 100) < 80)
							targetIdx = 0;

						submitToShard(shards[targetIdx], ownerKey, isWaitJob);
					}

					const int32 totalMailboxJobs = mailboxJobs * static_cast<int32>(std::max<size_t>(1, shards.size()));
					for (int32 i = 0; i < totalMailboxJobs; ++i)
					{
						const uint64 seq	   = submitSeq.fetch_add(1, std::memory_order_relaxed);
						const uint64 ownerKey  = SelectOwnerKey(spec, seq);
						const bool   isWaitJob = spec.waitEveryN > 0 && (((seq + 1) % static_cast<uint64>(spec.waitEveryN)) == 0);
						
						size_t targetIdx = static_cast<size_t>(ownerKey % std::max<size_t>(1, mailboxes.size()));
						if (spec.shardSkew && shards.size() > 1 && !spec.ownerContention && (seq % 100) < 80)
							targetIdx = 0;
						
						dispatchBenchJob([&](auto fn) { mailboxes[targetIdx]->Post(Job(std::move(fn))); }, ownerKey, isWaitJob, spec.costClass, spec.memoryTouch, spec.syncMode, spec.waitMode);
					}

					std::this_thread::sleep_for(std::chrono::milliseconds(spec.submitInterval_ms));
				}
			});

		std::this_thread::sleep_for(std::chrono::seconds(spec.duration_sec));

		produce.store(false, std::memory_order_release);
		if (producer.joinable())
			producer.join();

		const bool drained = WaitForScenarioDrain(
			shards,
			totalSubmitted,
			totalCompleted,
			60_s // 필요시 5~30s 조절
		);

		monitorRunning.store(false, std::memory_order_relaxed);
		if (monitor.joinable())
			monitor.join();

		if (!drained)
		{
			JAMNET_LOG_WARN(
				"[Scenario] {} drain timeout: submitted={}, completed={}, geQ={}, shardQSum may remain",
				spec.name,
				totalSubmitted.load(std::memory_order_acquire),
				totalCompleted.load(std::memory_order_acquire),
				GLOBAL_EXEC.GetQueueSize()
			);
		}

		auto geSnapshots	= GLOBAL_EXEC.GetMetricsSnapshot();
		auto shardSnapshots = GLOBAL_EXEC.GetShardMetricsSnapshots();

		double sumShardExecJobs			= 0.0;
		double sumShardIdlePct			= 0.0;
		double sumShardFiberRuns		= 0.0;
		double effectiveShardExecTotal	= 0.0;
		uint64 totalMailboxJobs			= 0;
		uint64 totalTickCatchUp			= 0;

		for (const auto& shard : shardSnapshots)
		{
			sumShardExecJobs  += static_cast<double>(shard.processJobsExecCount);
			sumShardFiberRuns += static_cast<double>(shard.schedulerReadyRunCount);
			totalMailboxJobs  += shard.mailboxJobMoveCount;
			totalTickCatchUp  += shard.tickCatchUpCount;

			const double idlePct = shard.loopCount == 0 ? 0.0 : (static_cast<double>(shard.idleLoopCount) / static_cast<double>(shard.loopCount)) * 100.0;
			sumShardIdlePct += idlePct;
		}

		effectiveShardExecTotal = spec.useFiberPath ? sumShardFiberRuns : sumShardExecJobs;

		const double shardCount = shardSnapshots.empty() ? 1.0 : static_cast<double>(shardSnapshots.size());

		// Calculate Tracking Result
		const size_t totalCollected = std::min(traceIdx.load(std::memory_order_relaxed), traces.size());
		std::vector<uint64> latencies;
		std::vector<uint64> waits;
		latencies.reserve(totalCollected);
		waits.reserve(totalCollected);

		uint64 sumLatency   = 0;
		uint64 sumQueueWait = 0;
		for (size_t i = 0; i < totalCollected; ++i)
		{
			const auto& t = traces[i];
			const uint64 lat = t.endTs - t.enqueueTs;
			const uint64 qw  = t.startTs - t.enqueueTs;
			latencies.push_back(lat);
			waits.push_back(qw);

			sumLatency   += lat;
			sumQueueWait += qw;
		}

		constexpr double k_missingMetric = -1.0;

		double p50    = k_missingMetric, p95 = k_missingMetric, p99 = k_missingMetric;
		double maxLat = k_missingMetric, avgLat = k_missingMetric;
		double avgQw  = k_missingMetric, p99Qw = k_missingMetric;

		double avgLockWait_us = k_missingMetric, p99LockWait_us = k_missingMetric, contendedLockPct = k_missingMetric;
		double runnableP99_us = k_missingMetric, waitP99_us = k_missingMetric;
		double hotOwnerHitPct = k_missingMetric;
		if (!latencies.empty())
		{
			std::ranges::sort(latencies);
			p50		= static_cast<double>(latencies[latencies.size() * 50 / 100]) / 1000.0;
			p95		= static_cast<double>(latencies[latencies.size() * 95 / 100]) / 1000.0;
			p99		= static_cast<double>(latencies[latencies.size() * 99 / 100]) / 1000.0;
			maxLat	= static_cast<double>(latencies.back()) / 1000.0;
			avgLat	= static_cast<double>(sumLatency / latencies.size()) / 1000.0;

			std::ranges::sort(waits);
			p99Qw = static_cast<double>(waits[waits.size() * 99 / 100]) / 1000.0;
			avgQw = static_cast<double>(sumQueueWait / waits.size()) / 1000.0;
		}

		if (totalCollected > 0)
		{
			std::vector<uint64> lockWaits;
			std::vector<uint64> runnableLats;
			std::vector<uint64> waitLats;
			lockWaits.reserve(totalCollected);
			runnableLats.reserve(totalCollected);
			waitLats.reserve(totalCollected);

			uint64 lockWaitSum = 0;
			size_t lockContendedCount = 0;
			for (size_t i = 0; i < totalCollected; ++i)
			{
				const auto& t = traces[i];
				lockWaits.push_back(t.lockWaitNs);
				lockWaitSum += t.lockWaitNs;
				if (t.lockWaitNs > 0)
					lockContendedCount++;

				const uint64 lat = t.endTs - t.enqueueTs;
				if (t.isWaitJob)
					waitLats.push_back(lat);
				else
					runnableLats.push_back(lat);
			}

			std::ranges::sort(lockWaits);
			avgLockWait_us   = static_cast<double>(lockWaitSum) / static_cast<double>(lockWaits.size()) / 1000.0;
			p99LockWait_us   = static_cast<double>(lockWaits[lockWaits.size() * 99 / 100]) / 1000.0;
			contendedLockPct = static_cast<double>(lockContendedCount) / static_cast<double>(totalCollected) * 100.0;

			if (!runnableLats.empty())
			{
				std::ranges::sort(runnableLats);
				runnableP99_us = static_cast<double>(runnableLats[runnableLats.size() * 99 / 100]) / 1000.0;
			}
			if (!waitLats.empty())
			{
				std::ranges::sort(waitLats);
				waitP99_us = static_cast<double>(waitLats[waitLats.size() * 99 / 100]) / 1000.0;
			}
		}

		const uint64 produced = totalProduced.load(std::memory_order_relaxed);
		if (produced > 0)
		{
			hotOwnerHitPct =
				static_cast<double>(hotOwnerHitCount.load(std::memory_order_relaxed)) /
				static_cast<double>(produced) * 100.0;
		}

		double avgQd = 0.0, maxQd = 0.0, shardMaxQd = 0.0;
		if (!queueSamples.empty())
		{
			size_t sumTot = 0, mTot = 0, mShard = 0;
			for (auto& qs : queueSamples)
			{
				size_t tot = qs.geQ + qs.shardTotalQ;
				sumTot += tot;
				mTot   = std::max(tot, mTot);
				mShard = std::max(qs.shardMaxQ, mShard);
			}
			avgQd	   = static_cast<double>(sumTot) / static_cast<double>(queueSamples.size());
			maxQd	   = static_cast<double>(mTot);
			shardMaxQd = static_cast<double>(mShard);
		}

		std::vector<double> shardTput;
		double meanTput = 0.0, stdTput = 0.0;
		for (const auto& s : shardSnapshots)
		{
			const double effectiveExecCount = spec.useFiberPath
				? static_cast<double>(s.schedulerReadyRunCount) : static_cast<double>(s.processJobsExecCount);

			double tput = effectiveExecCount / spec.duration_sec;
			shardTput.push_back(tput);
			meanTput += tput;
		}
		if (!shardTput.empty()) 
		{
			meanTput /= static_cast<double>(shardTput.size());
			for (double t : shardTput)
				stdTput += (t - meanTput) * (t - meanTput);
			stdTput = std::sqrt(stdTput / static_cast<double>(shardTput.size()));
		}

		ScenarioRow row{};
		row.scenario				= spec.name;
		row.duration_sec			= spec.duration_sec;
		row.avgLatency_us			= avgLat;
		row.p50Latency_us			= p50;
		row.p95Latency_us			= p95;
		row.p99Latency_us			= p99;
		row.maxLatency_us			= maxLat;
		row.avgQueueWait_us			= avgQw;
		row.p99QueueWait_us			= p99Qw;
		row.avgQueueDepth			= avgQd;
		row.maxQueueDepth			= maxQd;
		row.producerRateJobsPerS	= static_cast<double>(totalProduced.load()) / spec.duration_sec;
		row.completedJobsPerS		= (totalCollected > 0) ? static_cast<double>(totalCollected) / spec.duration_sec : k_missingMetric;
		row.shardMaxQueueDepth		= shardMaxQd;
		row.shardStddevThroughput	= stdTput;

		row.geJobsPerSec			= static_cast<double>(geSnapshots.workerJobExecCount) / static_cast<double>(spec.duration_sec);
		row.geIdlePct				= geSnapshots.workerLoopCount == 0 ? 0.0 : (static_cast<double>(geSnapshots.workerIdleLoopCount) / static_cast<double>(geSnapshots.workerLoopCount)) * 100.0;
		row.workerBusyPct			= std::max(0.0, 100.0 - row.geIdlePct);
		row.geFiberAvgPoll_us		= geSnapshots.fiberPollCount == 0 ? 0.0 : (static_cast<double>(geSnapshots.fiberPollCost_ns) / static_cast<double>(geSnapshots.fiberPollCount) / 1000.0);
		row.shardAvgExecJobsPerSec  = (effectiveShardExecTotal / shardCount) / static_cast<double>(spec.duration_sec);
		row.shardFiberRunsPerSec	= (sumShardFiberRuns / shardCount) / static_cast<double>(spec.duration_sec);
  		row.shardIdlePct			= sumShardIdlePct / shardCount;
		row.mailboxJobsPerSec		= static_cast<double>(totalMailboxJobs) / static_cast<double>(spec.duration_sec);
		row.tickCatchUpPerSec		= static_cast<double>(totalTickCatchUp) / static_cast<double>(spec.duration_sec);
		row.avgLockWait_us			= avgLockWait_us;
		row.p99LockWait_us			= p99LockWait_us;
		row.contendedLockPct		= contendedLockPct;
		row.runnableP99_us			= runnableP99_us;
		row.waitP99_us				= waitP99_us;
		row.hotOwnerHitPct			= hotOwnerHitPct;

		// Burst trace build & backlog recovery time
		if (spec.burst && !latencies.empty())
		{
			std::vector<JobTrace> validTraces(traces.begin(), traces.begin() + totalCollected);
			ranges::sort(validTraces, [](const JobTrace& a, const JobTrace& b) { return a.endTs < b.endTs; });

			struct TBucket
			{
				uint64_t				endCnt = 0;
				std::vector<uint64_t>	lats;
				size_t					maxQ = 0;
			};

			std::map<uint64_t, TBucket> buckets;
			constexpr uint64 bucketSizeNs = 100'000'000ULL; // 100ms

			for (const auto& vt : validTraces)
			{
				uint64 bIdx = (vt.endTs - warmupEndTs) / bucketSizeNs;
				buckets[bIdx].endCnt++;
				buckets[bIdx].lats.push_back(vt.endTs - vt.enqueueTs);
			}
			for (const auto& qs : queueSamples)
			{
				uint64 bIdx = (qs.timestamp_ns - warmupEndTs) / bucketSizeNs;
				size_t tq = qs.geQ + qs.shardTotalQ;
				buckets[bIdx].maxQ = std::max(tq, buckets[bIdx].maxQ);
			}

			uint64 peakTime_ms = 0;
			size_t peakQ = 0;
			bool recovered = false;
			uint64 recoveryTime_ms = 0;

			row.hasBurstTrace = true;
			row.burstTraceRows.clear();
			row.burstTraceRows.reserve(buckets.size());

			for (auto& kv : buckets)
			{
				auto& b = kv.second;
				double bp99 = 0.0;
				if (!b.lats.empty())
				{
					size_t idx = b.lats.size() * 99 / 100;
					ranges::nth_element(b.lats, b.lats.begin() + idx);
					bp99 = static_cast<double>(b.lats[idx]) / 1000.0;
				}

				const uint64 tMs = kv.first * 100;

				row.burstTraceRows.push_back({ 
					.timeBucket_ms = tMs, 
					.completedJobs = b.endCnt, 
					.p99Latency_us = bp99, 
					.queueDepthMax = b.maxQ });

				if (!recovered)
				{
					if (b.maxQ > peakQ)
					{
						peakQ = b.maxQ;
						peakTime_ms = tMs;
					}
					else if (peakQ > 0 && tMs > peakTime_ms + 500 && static_cast<double>(b.maxQ) <= avgQd)
					{
						recoveryTime_ms = tMs - peakTime_ms;
						recovered = true;
					}
				}
			}

			row.backlogRecoveryTime_ms = recovered ? static_cast<double>(recoveryTime_ms) : -1.0;
		}

		for (size_t i = 0; i < shards.size(); ++i)
		{
			if (mailboxes[i])
				shards[i]->RemoveMailbox(mailboxes[i]->GetId());
		}

		GLOBAL_EXEC.ResetMetrics();
		return row;
	}

	bool IsArchScenario(const ScenarioSpec& sc)
	{
		return sc.name.rfind("arch_", 0) == 0;
	}

	std::string MakeRunStamp()
	{
		using namespace std::chrono;

		const auto now = system_clock::now();
		const auto tt = system_clock::to_time_t(now);

		std::tm localTm{};
#if defined(_WIN32)
		localtime_s(&localTm, &tt);
#else
		localtime_r(&tt, &localTm);
#endif

		std::ostringstream oss;
		oss << std::put_time(&localTm, "%Y-%m-%d %H:%M:%S");
		return oss.str();
	}

	void AppendCsvRow(
		const std::string& csvPath,
		const ScenarioRow& row,
		bool writeRunHeader,
		const std::string& runStamp)
	{
		const bool hasFile = std::filesystem::exists(csvPath);
		std::ofstream out(csvPath, std::ios::out | std::ios::app);
		if (!out.is_open())
		{
			JAMNET_LOG_ERROR("Failed to open csv: {}", csvPath);
			return;
		}

		if (hasFile && writeRunHeader)
			out << "\n";

		if (writeRunHeader)
			out << "# Run started at: " << runStamp << "\n";

		if (!hasFile || writeRunHeader)
		{
			out << "scenario,duration_s,p50_us,p95_us,p99_us,avg_lat_us,avg_wait_us,p99_wait_us,"
				<< "avg_q_depth,max_q_depth,producer_rate_s,completed_jobs_s,shard_max_q,shard_stddev,worker_busy_pct,recovery_ms,"
				<< "ge_jobs_per_s,ge_idle_pct,ge_fiber_poll_us,shard_exec_jobs_s,shard_fiber_runs_s,shard_idle_pct,mailbox_jobs_s,tick_catchup_s,"
				<< "avg_lock_wait_us,p99_lock_wait_us,lock_contended_pct,runnable_p99_us,wait_p99_us,hot_owner_hit_pct\n";
		}

		out << row.scenario << ","
			<< row.duration_sec << ","
			<< std::format("{:.2f}", row.p50Latency_us) << ","
			<< std::format("{:.2f}", row.p95Latency_us) << ","
			<< std::format("{:.2f}", row.p99Latency_us) << ","
			<< std::format("{:.2f}", row.avgLatency_us) << ","
			<< std::format("{:.2f}", row.avgQueueWait_us) << ","
			<< std::format("{:.2f}", row.p99QueueWait_us) << ","
			<< std::format("{:.2f}", row.avgQueueDepth) << ","
			<< std::format("{:.2f}", row.maxQueueDepth) << ","
			<< std::format("{:.2f}", row.producerRateJobsPerS) << ","
			<< std::format("{:.2f}", row.completedJobsPerS) << ","
			<< std::format("{:.2f}", row.shardMaxQueueDepth) << ","
			<< std::format("{:.2f}", row.shardStddevThroughput) << ","
			<< std::format("{:.2f}", row.workerBusyPct) << ","
			<< std::format("{:.2f}", row.backlogRecoveryTime_ms) << ","
			<< std::format("{:.2f}", row.geJobsPerSec) << ","
			<< std::format("{:.2f}", row.geIdlePct) << ","
			<< std::format("{:.2f}", row.geFiberAvgPoll_us) << ","
			<< std::format("{:.2f}", row.shardAvgExecJobsPerSec) << ","
			<< std::format("{:.2f}", row.shardFiberRunsPerSec) << ","
			<< std::format("{:.2f}", row.shardIdlePct) << ","
			<< std::format("{:.2f}", row.mailboxJobsPerSec) << ","
			<< std::format("{:.2f}", row.tickCatchUpPerSec) << ","
			<< std::format("{:.2f}", row.avgLockWait_us) << ","
			<< std::format("{:.2f}", row.p99LockWait_us) << ","
			<< std::format("{:.2f}", row.contendedLockPct) << ","
			<< std::format("{:.2f}", row.runnableP99_us) << ","
			<< std::format("{:.2f}", row.waitP99_us) << ","
			<< std::format("{:.2f}", row.hotOwnerHitPct) << "\n";

		JAMNET_LOG_INFO("[Result] p50={:.2f}us, p99={:.2f}us, maxQ={:.2f}, recoverMs={:.2f}",
			row.p50Latency_us, row.p99Latency_us, row.maxQueueDepth, row.backlogRecoveryTime_ms);

		if (!row.hasBurstTrace || row.burstTraceRows.empty())
			return;

		const std::string burstCsvPath = "burst_timeseries.csv";
		const bool burstHasFile = std::filesystem::exists(burstCsvPath);

		std::ofstream bout(burstCsvPath, std::ios::out | std::ios::app);
		if (!bout.is_open())
		{
			JAMNET_LOG_ERROR("Failed to open burst csv: {}", burstCsvPath);
			return;
		}

		if (burstHasFile && writeRunHeader)
			bout << "\n";

		if (writeRunHeader)
			bout << "# Run started at: " << runStamp << "\n";

		if (!burstHasFile || writeRunHeader)
			bout << "scenario,time_bucket_ms,jobs_completed,p99_latency_us,queue_depth_max\n";

		for (const auto& b : row.burstTraceRows)
		{
			bout << row.scenario << ","
				<< b.timeBucket_ms << ","
				<< b.completedJobs << ","
				<< std::format("{:.2f}", b.p99Latency_us) << ","
				<< b.queueDepthMax << "\n";
		}
	}

	bool HasArg(const std::vector<std::string>& args, std::string_view key)
	{
		return ranges::find(args, key) != args.end();
	}

	std::string GetArgValue(const std::vector<std::string>& args, std::string_view key, const std::string& def)
	{
		for (size_t i = 0; i + 1 < args.size(); ++i)
		{
			if (args[i] == key)
				return args[i + 1];
		}
		return def;
	}
}

int main(int argc, char** argv)
{
	vector<string> args;
	args.reserve(static_cast<size_t>(std::max(0, argc - 1)));
	for (int i = 1; i < argc; ++i)
		args.emplace_back(argv[i]);

	const bool measureAll		= HasArg(args, "--measure-all");
	const bool measureArch		= HasArg(args, "--measure-arch");
	const bool measureNonArch	= HasArg(args, "--measure-non-arch");

	const int32 defaultDurationSec	= std::max<int32>(1, std::stoi(GetArgValue(args, "--duration", "60")));
	const int32 archDurationSec		= std::max<int32>(1, std::stoi(GetArgValue(args, "--arch-duration", std::to_string(defaultDurationSec))));
	const int32 nonArchDurationSec	= std::max<int32>(1, std::stoi(GetArgValue(args, "--non-arch-duration", std::to_string(defaultDurationSec))));
	const int32 repeatCount			= std::max<int32>(1, std::stoi(GetArgValue(args, "--repeat", "1")));
	const string csvPath			= GetArgValue(args, "--csv", "executor_metrics.csv");

	net::RuntimeConfig config{};
	config.geConfig = {
		.autoTune  = true,
		.layoutCfg = {
			.mode			  = Balance,
			.reservedThreads = 1,
			.profile		  = CoreProfileServer,
		}
	};
	net::NetRuntime runtime(config);

	if (measureAll || measureArch || measureNonArch)
	{
		JAMNET_LOG_INFO("Auto measure mode started. csv= {}", csvPath);

		std::vector<ScenarioSpec> scenarios = {

			// arch
			{ .name = "arch_owner_shard",       .duration_sec = archDurationSec,    .submitInterval_ms = 1, .geJobsPerTick = 0, .shardJobsPerTick = 24, .mailboxJobsPerTick = 0, .burst = false, .costClass = 2, .memoryTouch = false, .shardSkew = false, .ownerContention = true, .ownerPoolSize = 1024, .hotOwnerPoolSize = 16, .waitEveryN = 0, .useFiberPath = false, .syncMode = SyncMode::ShardSerial, .waitMode = WaitMode::None },
			{ .name = "arch_owner_lock",        .duration_sec = archDurationSec,    .submitInterval_ms = 1, .geJobsPerTick = 0, .shardJobsPerTick = 24, .mailboxJobsPerTick = 0, .burst = false, .costClass = 2, .memoryTouch = false, .shardSkew = false, .ownerContention = true, .ownerPoolSize = 1024, .hotOwnerPoolSize = 16, .waitEveryN = 0, .useFiberPath = false, .syncMode = SyncMode::TwoStageLock, .waitMode = WaitMode::None },
			  
			{ .name = "arch_thread_block_25p",  .duration_sec = archDurationSec,    .submitInterval_ms = 1, .geJobsPerTick = 0, .shardJobsPerTick = 16, .mailboxJobsPerTick = 0, .burst = false, .costClass = 1, .memoryTouch = false, .shardSkew = false, .ownerContention = false, .waitEveryN = 4, .useFiberPath = true, .syncMode = SyncMode::ShardSerial, .waitMode = WaitMode::ThreadBlock },
			{ .name = "arch_fiber_suspend_25p", .duration_sec = archDurationSec,    .submitInterval_ms = 1, .geJobsPerTick = 0, .shardJobsPerTick = 16, .mailboxJobsPerTick = 0, .burst = false, .costClass = 1, .memoryTouch = false, .shardSkew = false, .ownerContention = false, .waitEveryN = 4, .useFiberPath = true, .syncMode = SyncMode::ShardSerial, .waitMode = WaitMode::FiberSuspend },
			{ .name = "arch_waitmix_block",     .duration_sec = archDurationSec,    .submitInterval_ms = 1, .geJobsPerTick = 0, .shardJobsPerTick = 24, .mailboxJobsPerTick = 0, .burst = false, .costClass = 1, .memoryTouch = false, .shardSkew = false, .ownerContention = false, .waitEveryN = 4, .useFiberPath = true, .syncMode = SyncMode::ShardSerial, .waitMode = WaitMode::ThreadBlock },
			{ .name = "arch_waitmix_fiber",     .duration_sec = archDurationSec,    .submitInterval_ms = 1, .geJobsPerTick = 0, .shardJobsPerTick = 24, .mailboxJobsPerTick = 0, .burst = false, .costClass = 1, .memoryTouch = false, .shardSkew = false, .ownerContention = false, .waitEveryN = 4, .useFiberPath = true, .syncMode = SyncMode::ShardSerial, .waitMode = WaitMode::FiberSuspend },
			  
			// non-arch
			{ .name = "steady_light",			.duration_sec = nonArchDurationSec, .submitInterval_ms = 1, .geJobsPerTick = 192, .shardJobsPerTick = 48, .mailboxJobsPerTick = 48, .burst = false, .costClass = 2, .memoryTouch = false, .shardSkew = false, .ownerContention = false },
			{ .name = "steady_near_limit",		.duration_sec = nonArchDurationSec, .submitInterval_ms = 1, .geJobsPerTick = 256, .shardJobsPerTick = 64, .mailboxJobsPerTick = 64, .burst = false, .costClass = 2, .memoryTouch = false, .shardSkew = false, .ownerContention = false },
			{ .name = "steady_memtouch",		.duration_sec = nonArchDurationSec, .submitInterval_ms = 1, .geJobsPerTick = 32,  .shardJobsPerTick = 8,  .mailboxJobsPerTick = 8,  .burst = false, .costClass = 3, .memoryTouch = true,  .shardSkew = false, .ownerContention = false },

			{ .name = "burst_light",			.duration_sec = nonArchDurationSec, .submitInterval_ms = 1, .geJobsPerTick = 128, .shardJobsPerTick = 32, .mailboxJobsPerTick = 32, .burst = true,  .costClass = 2, .memoryTouch = false, .shardSkew = false, .ownerContention = false },
			{ .name = "burst_near_limit",		.duration_sec = nonArchDurationSec, .submitInterval_ms = 1, .geJobsPerTick = 192, .shardJobsPerTick = 48, .mailboxJobsPerTick = 48, .burst = true,  .costClass = 2, .memoryTouch = false, .shardSkew = false, .ownerContention = false },
			{ .name = "burst_hot_shard",		.duration_sec = nonArchDurationSec, .submitInterval_ms = 1, .geJobsPerTick = 64,  .shardJobsPerTick = 16, .mailboxJobsPerTick = 16, .burst = true,  .costClass = 1, .memoryTouch = false, .shardSkew = true,  .ownerContention = false },
			{ .name = "burst_owner_hotspot",	.duration_sec = nonArchDurationSec, .submitInterval_ms = 1, .geJobsPerTick = 0,   .shardJobsPerTick = 16, .mailboxJobsPerTick = 8,  .burst = true,  .costClass = 1, .memoryTouch = false, .shardSkew = false, .ownerContention = true, .ownerPoolSize = 2048, .hotOwnerPoolSize = 8 },

			{ .name = "saturation_360k",        .duration_sec = nonArchDurationSec, .submitInterval_ms = 1, .geJobsPerTick = 288, .shardJobsPerTick = 72, .mailboxJobsPerTick = 72, .burst = false, .costClass = 2 },
			{ .name = "saturation_370k",        .duration_sec = nonArchDurationSec, .submitInterval_ms = 1, .geJobsPerTick = 296, .shardJobsPerTick = 74, .mailboxJobsPerTick = 74, .burst = false, .costClass = 2 },
			{ .name = "saturation_380k",        .duration_sec = nonArchDurationSec, .submitInterval_ms = 1, .geJobsPerTick = 304, .shardJobsPerTick = 76, .mailboxJobsPerTick = 76, .burst = false, .costClass = 2 },
			{ .name = "saturation_390k",        .duration_sec = nonArchDurationSec, .submitInterval_ms = 1, .geJobsPerTick = 312, .shardJobsPerTick = 78, .mailboxJobsPerTick = 78, .burst = false, .costClass = 2 },
			{ .name = "saturation_400k",        .duration_sec = nonArchDurationSec, .submitInterval_ms = 1, .geJobsPerTick = 320, .shardJobsPerTick = 80, .mailboxJobsPerTick = 80, .burst = false, .costClass = 2 },
			
			{ .name = "mix_memtouch",           .duration_sec = nonArchDurationSec, .submitInterval_ms = 1, .geJobsPerTick = 32,  .shardJobsPerTick = 8,  .mailboxJobsPerTick = 8,  .burst = false, .costClass = 3, .memoryTouch = true,  .ownerContention = false },
			{ .name = "hot_shard",              .duration_sec = nonArchDurationSec, .submitInterval_ms = 1, .geJobsPerTick = 32,  .shardJobsPerTick = 8,  .mailboxJobsPerTick = 8,  .burst = false, .costClass = 1, .memoryTouch = false, .shardSkew = true },
			{ .name = "owner_hotspot",          .duration_sec = nonArchDurationSec, .submitInterval_ms = 1, .geJobsPerTick = 0,   .shardJobsPerTick = 16, .mailboxJobsPerTick = 8,  .burst = false, .costClass = 1, .memoryTouch = false, .shardSkew = false, .ownerContention = true, .ownerPoolSize = 2048, .hotOwnerPoolSize = 8, .syncMode = SyncMode::ShardSerial }
		};

		if (measureArch && !measureAll)
		{
			std::erase_if(scenarios, [](const ScenarioSpec& sc) { return !IsArchScenario(sc); });
		}
		else if (measureNonArch && !measureAll)
		{
			std::erase_if(scenarios, [](const ScenarioSpec& sc) { return IsArchScenario(sc); });
		}


		JAMNET_LOG_INFO("Measure mode started. csv={}, repeat={}, archDuration={}s, nonArchDuration={}s, mode(all={}, arch={}, nonArch={})",
			csvPath,
			repeatCount,
			archDurationSec,
			nonArchDurationSec,
			measureAll,
			measureArch,
			measureNonArch);

		for (int32 rep = 0; rep < repeatCount; ++rep)
		{
			const std::string runStamp = MakeRunStamp();
			JAMNET_LOG_INFO("[Run {}/{}] start at {}", rep + 1, repeatCount, runStamp);

			bool firstRowOfThisRun = true;

			for (const auto& sc : scenarios)
			{
				JAMNET_LOG_INFO("[Run {}/{}][Scenario] {} begin", rep + 1, repeatCount, sc.name);

				auto row = RunScenario(sc);
				AppendCsvRow(csvPath, row, firstRowOfThisRun, runStamp);
				firstRowOfThisRun = false;

				JAMNET_LOG_INFO("[Run {}/{}][Scenario] {} done: GE jobs/s={:.3f}, GE idle={:.3f}%, GE fiber poll={:.3f}us, Shard exec jobs/s={:.3f}, Shard idle={:.3f}%, Mailbox jobs/s={:.3f}, Tick catchUp/s={:.3f}",
					rep + 1,
					repeatCount,
					row.scenario,
					row.geJobsPerSec,
					row.geIdlePct,
					row.geFiberAvgPoll_us,
					row.shardAvgExecJobsPerSec,
					row.shardIdlePct,
					row.mailboxJobsPerSec,
					row.tickCatchUpPerSec);
			}
		}

		JAMNET_LOG_INFO("Measure mode finished. csv={}", csvPath);
		return 0;
	}
}