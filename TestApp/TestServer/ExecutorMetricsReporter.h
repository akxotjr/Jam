#pragma once

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "jambase/JamTypes.h"
#include "jamnet/core/executor/ExecutorMetrics.h"
#include "jamnet/core/executor/GlobalExecutor.h"
#include "jamnet/core/utils/Clock.h"

namespace testserver
{
	struct ExecutorMetricsRow
	{
		uint64		captureEpochMs			= 0;
		std::string	scope;
		int32		shardIndex				= -1;
		double		intervalMs				= 0.0;

		double		loopPerSec				= 0.0;
		double		jobExecPerSec			= 0.0;
		double		idleRatio				= 0.0;
		double		avgJobExecCost_us		= 0.0;
		double		avgWaitCost_us			= 0.0;

		double		fiberPollPerSec			= 0.0;
		double		fiberEmptyPollRatio		= 0.0;
		double		fiberReadyRunPerSec		= 0.0;
		double		avgFiberPollCost_us		= 0.0;
		double		avgFiberSleepCost_us	= 0.0;

		double		ingressBatchPerSec		= 0.0;
		double		ingressJobPerSec		= 0.0;
		double		processJobsPerSec		= 0.0;
		double		mailboxProcessPerSec	= 0.0;
		double		mailboxJobMovePerSec	= 0.0;
		double		schedulerPollPerSec		= 0.0;
		double		schedulerEmptyPollRatio	= 0.0;
		double		schedulerReadyRunPerSec	= 0.0;
		double		avgSchedulerPollCost_us	= 0.0;
		double		tickPerSec				= 0.0;
		double		tickCatchUpPerSec		= 0.0;
	};

	inline uint64 NowEpochMs()
	{
		const auto now = std::chrono::system_clock::now();
		return static_cast<uint64>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
	}

	inline std::string MakeRunTimestamp()
	{
		const auto now = std::chrono::system_clock::now();
		const std::time_t tt = std::chrono::system_clock::to_time_t(now);
		std::tm local{};
		localtime_s(&local, &tt);

		std::ostringstream oss;
		oss << std::put_time(&local, "%Y%m%d_%H%M%S");
		return oss.str();
	}

	inline uint64 SubCounter(uint64 now, uint64 prev)
	{
		return (now >= prev) ? (now - prev) : 0;
	}

	inline double PerSec(uint64 count, double seconds)
	{
		return (seconds > 0.0) ? (static_cast<double>(count) / seconds) : 0.0;
	}

	inline double Ratio(uint64 num, uint64 den)
	{
		return (den > 0) ? (static_cast<double>(num) / static_cast<double>(den)) : 0.0;
	}

	inline double AvgCostUs(uint64 cost_ns, uint64 count)
	{
		return (count > 0) ? (static_cast<double>(cost_ns) / static_cast<double>(count) / 1000.0) : 0.0;
	}

	inline jam::ExecutorMetricsSampleEvent CaptureExecutorMetricsSample()
	{
		jam::ExecutorMetricsSampleEvent ev{};
		ev.sampleTime_ns = NOW_NS();
		ev.global = GLOBAL_EXEC.GetMetricsSnapshot();
		ev.shards = GLOBAL_EXEC.GetShardMetricsSnapshots();
		return ev;
	}

	class ExecutorCsvReporter
	{
	public:
		ExecutorCsvReporter()
		{
			const std::string stamp = MakeRunTimestamp();
			m_dir = std::filesystem::path("Reports") / "ExecutorMetrics";
			std::filesystem::create_directories(m_dir);

			m_csvPath     = m_dir / ("server_executor_metrics_" + stamp + ".csv");
			m_summaryPath = m_dir / ("server_executor_metrics_" + stamp + "_summary.csv");

			m_csv.open(m_csvPath, std::ios::out | std::ios::trunc);
			WriteHeader();
		}

		~ExecutorCsvReporter()
		{
			if (m_csv.is_open())
				m_csv.flush();
		}

		void WriteSample(const jam::ExecutorMetricsSampleEvent& ev)
		{
			if (!m_csv.is_open())
				return;

			if (m_hasPrevGlobal)
			{
				const double intervalSec = NsToSec(SubCounter(ev.sampleTime_ns, m_prevSampleTime_ns));
				WriteRow(MakeGlobalRow(ev.global, m_prevGlobal, intervalSec));
			}

			for (const auto& shard : ev.shards)
			{
				auto it = m_prevShards.find(shard.shardIndex);
				if (it != m_prevShards.end())
				{
					const double intervalSec = NsToSec(SubCounter(ev.sampleTime_ns, m_prevSampleTime_ns));
					WriteRow(MakeShardRow(shard, it->second, intervalSec));
				}

				m_prevShards[shard.shardIndex] = shard;
			}

			m_prevGlobal = ev.global;
			m_prevSampleTime_ns = ev.sampleTime_ns;
			m_hasPrevGlobal = true;
			m_csv.flush();
		}

		void WriteSummary()
		{
			struct Agg
			{
				uint64 count = 0;
				double sumLoopPerSec = 0.0;
				double sumJobExecPerSec = 0.0;
				double sumIdleRatio = 0.0;
				double sumAvgJobExecCostUs = 0.0;
				double maxAvgJobExecCostUs = 0.0;
				double sumFiberReadyRunPerSec = 0.0;
				double sumSchedulerReadyRunPerSec = 0.0;
				double sumTickPerSec = 0.0;
				double sumTickCatchUpPerSec = 0.0;
			};

			std::unordered_map<std::string, Agg> table;
			for (const auto& row : m_rows)
			{
				const std::string key = row.scope + "|" + std::to_string(row.shardIndex);
				auto& a = table[key];
				a.count++;
				a.sumLoopPerSec += row.loopPerSec;
				a.sumJobExecPerSec += row.jobExecPerSec;
				a.sumIdleRatio += row.idleRatio;
				a.sumAvgJobExecCostUs += row.avgJobExecCost_us;
				a.maxAvgJobExecCostUs = std::max(a.maxAvgJobExecCostUs, row.avgJobExecCost_us);
				a.sumFiberReadyRunPerSec += row.fiberReadyRunPerSec;
				a.sumSchedulerReadyRunPerSec += row.schedulerReadyRunPerSec;
				a.sumTickPerSec += row.tickPerSec;
				a.sumTickCatchUpPerSec += row.tickCatchUpPerSec;
			}

			std::ofstream summary(m_summaryPath, std::ios::out | std::ios::trunc);
			summary << "scope,shardIndex,count,avgLoopPerSec,avgJobExecPerSec,avgIdleRatio,avgJobExecCost_us,maxAvgJobExecCost_us,avgFiberReadyRunPerSec,avgSchedulerReadyRunPerSec,avgTickPerSec,avgTickCatchUpPerSec\n";
			for (const auto& [key, a] : table)
			{
				const double n = static_cast<double>(a.count);
				const auto sep = key.find('|');
				const std::string scope = key.substr(0, sep);
				const std::string shardIndex = (sep == std::string::npos) ? "-1" : key.substr(sep + 1);
				summary
					<< scope << ","
					<< shardIndex << ","
					<< a.count << ","
					<< (a.sumLoopPerSec / n) << ","
					<< (a.sumJobExecPerSec / n) << ","
					<< (a.sumIdleRatio / n) << ","
					<< (a.sumAvgJobExecCostUs / n) << ","
					<< a.maxAvgJobExecCostUs << ","
					<< (a.sumFiberReadyRunPerSec / n) << ","
					<< (a.sumSchedulerReadyRunPerSec / n) << ","
					<< (a.sumTickPerSec / n) << ","
					<< (a.sumTickCatchUpPerSec / n) << "\n";
			}
		}

	private:
		static double NsToSec(uint64 ns)
		{
			return static_cast<double>(ns) / 1'000'000'000.0;
		}

		static ExecutorMetricsRow MakeGlobalRow(const jam::GlobalExecutorMetrics& now, const jam::GlobalExecutorMetrics& prev, double intervalSec)
		{
			const uint64 workerLoopCount = SubCounter(now.workerLoopCount, prev.workerLoopCount);
			const uint64 workerJobExecCount = SubCounter(now.workerJobExecCount, prev.workerJobExecCount);
			const uint64 workerIdleLoopCount = SubCounter(now.workerIdleLoopCount, prev.workerIdleLoopCount);
			const uint64 workerWaitCost_ns = SubCounter(now.workerWaitCost_ns, prev.workerWaitCost_ns);
			const uint64 workerJobExecCost_ns = SubCounter(now.workerJobExecCost_ns, prev.workerJobExecCost_ns);
			const uint64 fiberPollCount = SubCounter(now.fiberPollCount, prev.fiberPollCount);
			const uint64 fiberEmptyPollCount = SubCounter(now.fiberEmptyPollCount, prev.fiberEmptyPollCount);
			const uint64 fiberPollCost_ns = SubCounter(now.fiberPollCost_ns, prev.fiberPollCost_ns);
			const uint64 fiberSleepCost_ns = SubCounter(now.fiberSleepCost_ns, prev.fiberSleepCost_ns);

			ExecutorMetricsRow row{};
			row.captureEpochMs = NowEpochMs();
			row.scope = "global";
			row.shardIndex = -1;
			row.intervalMs = intervalSec * 1000.0;
			row.loopPerSec = PerSec(workerLoopCount, intervalSec);
			row.jobExecPerSec = PerSec(workerJobExecCount, intervalSec);
			row.idleRatio = Ratio(workerIdleLoopCount, workerLoopCount);
			row.avgJobExecCost_us = AvgCostUs(workerJobExecCost_ns, workerJobExecCount);
			row.avgWaitCost_us = AvgCostUs(workerWaitCost_ns, workerIdleLoopCount);
			row.fiberPollPerSec = PerSec(fiberPollCount, intervalSec);
			row.fiberEmptyPollRatio = Ratio(fiberEmptyPollCount, fiberPollCount);
			row.fiberReadyRunPerSec = PerSec(SubCounter(now.fiberReadyRunCount, prev.fiberReadyRunCount), intervalSec);
			row.avgFiberPollCost_us = AvgCostUs(fiberPollCost_ns, fiberPollCount);
			row.avgFiberSleepCost_us = AvgCostUs(fiberSleepCost_ns, SubCounter(now.fiberEmptyPollCount, prev.fiberEmptyPollCount));
			return row;
		}

		static ExecutorMetricsRow MakeShardRow(const jam::ShardExecutorMetrics& now, const jam::ShardExecutorMetrics& prev, double intervalSec)
		{
			const uint64 loopCount = SubCounter(now.loopCount, prev.loopCount);
			const uint64 idleLoopCount = SubCounter(now.idleLoopCount, prev.idleLoopCount);
			const uint64 schedulerPollCount = SubCounter(now.schedulerPollCount, prev.schedulerPollCount);

			ExecutorMetricsRow row{};
			row.captureEpochMs = NowEpochMs();
			row.scope = "shard";
			row.shardIndex = now.shardIndex;
			row.intervalMs = intervalSec * 1000.0;
			row.loopPerSec = PerSec(loopCount, intervalSec);
			row.jobExecPerSec = PerSec(SubCounter(now.processJobsExecCount, prev.processJobsExecCount), intervalSec);
			row.idleRatio = Ratio(idleLoopCount, loopCount);
			row.avgWaitCost_us = AvgCostUs(SubCounter(now.idleSleepCost_ns, prev.idleSleepCost_ns), idleLoopCount);
			row.ingressBatchPerSec = PerSec(SubCounter(now.ingressBatchCount, prev.ingressBatchCount), intervalSec);
			row.ingressJobPerSec = PerSec(SubCounter(now.ingressJobCount, prev.ingressJobCount), intervalSec);
			row.processJobsPerSec = PerSec(SubCounter(now.processJobsCallCount, prev.processJobsCallCount), intervalSec);
			row.mailboxProcessPerSec = PerSec(SubCounter(now.mailboxProcessCount, prev.mailboxProcessCount), intervalSec);
			row.mailboxJobMovePerSec = PerSec(SubCounter(now.mailboxJobMoveCount, prev.mailboxJobMoveCount), intervalSec);
			row.schedulerPollPerSec = PerSec(schedulerPollCount, intervalSec);
			row.schedulerEmptyPollRatio = Ratio(SubCounter(now.schedulerEmptyPollCount, prev.schedulerEmptyPollCount), schedulerPollCount);
			row.schedulerReadyRunPerSec = PerSec(SubCounter(now.schedulerReadyRunCount, prev.schedulerReadyRunCount), intervalSec);
			row.avgSchedulerPollCost_us = AvgCostUs(SubCounter(now.schedulerPollCost_ns, prev.schedulerPollCost_ns), schedulerPollCount);
			row.tickPerSec = PerSec(SubCounter(now.tickCount, prev.tickCount), intervalSec);
			row.tickCatchUpPerSec = PerSec(SubCounter(now.tickCatchUpCount, prev.tickCatchUpCount), intervalSec);
			return row;
		}

		void WriteHeader()
		{
			m_csv << "captureEpochMs,scope,shardIndex,intervalMs,"
				<< "loopPerSec,jobExecPerSec,idleRatio,avgJobExecCost_us,avgWaitCost_us,"
				<< "fiberPollPerSec,fiberEmptyPollRatio,fiberReadyRunPerSec,avgFiberPollCost_us,avgFiberSleepCost_us,"
				<< "ingressBatchPerSec,ingressJobPerSec,processJobsPerSec,mailboxProcessPerSec,mailboxJobMovePerSec,"
				<< "schedulerPollPerSec,schedulerEmptyPollRatio,schedulerReadyRunPerSec,avgSchedulerPollCost_us,"
				<< "tickPerSec,tickCatchUpPerSec\n";
		}

		void WriteRow(const ExecutorMetricsRow& row)
		{
			m_rows.push_back(row);
			m_csv
				<< row.captureEpochMs << ","
				<< row.scope << ","
				<< row.shardIndex << ","
				<< row.intervalMs << ","
				<< row.loopPerSec << ","
				<< row.jobExecPerSec << ","
				<< row.idleRatio << ","
				<< row.avgJobExecCost_us << ","
				<< row.avgWaitCost_us << ","
				<< row.fiberPollPerSec << ","
				<< row.fiberEmptyPollRatio << ","
				<< row.fiberReadyRunPerSec << ","
				<< row.avgFiberPollCost_us << ","
				<< row.avgFiberSleepCost_us << ","
				<< row.ingressBatchPerSec << ","
				<< row.ingressJobPerSec << ","
				<< row.processJobsPerSec << ","
				<< row.mailboxProcessPerSec << ","
				<< row.mailboxJobMovePerSec << ","
				<< row.schedulerPollPerSec << ","
				<< row.schedulerEmptyPollRatio << ","
				<< row.schedulerReadyRunPerSec << ","
				<< row.avgSchedulerPollCost_us << ","
				<< row.tickPerSec << ","
				<< row.tickCatchUpPerSec << "\n";
		}

	private:
		std::filesystem::path m_dir;
		std::filesystem::path m_csvPath;
		std::filesystem::path m_summaryPath;
		std::ofstream m_csv;
		std::vector<ExecutorMetricsRow> m_rows;
		jam::GlobalExecutorMetrics m_prevGlobal = {};
		std::unordered_map<int32, jam::ShardExecutorMetrics> m_prevShards;
		uint64 m_prevSampleTime_ns = 0;
		bool m_hasPrevGlobal = false;
	};
}
