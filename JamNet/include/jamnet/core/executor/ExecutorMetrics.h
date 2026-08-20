#pragma once
#include "jamnet/core/executor/Job.h"
#include "jamnet/core/utils/TimeUnits.h"

namespace jam
{
	struct GlobalExecutorMetrics
	{
		uint64 workerLoopCount          = 0;
		uint64 workerJobExecCount       = 0;
		uint64 workerIdleLoopCount      = 0;
		uint64 workerWaitCost_ns        = 0_ns;
		uint64 workerJobExecCost_ns     = 0_ns;

		uint64 fiberLoopCount           = 0;
		uint64 fiberPollCount           = 0;
		uint64 fiberEmptyPollCount      = 0;
		uint64 fiberPollCost_ns         = 0_ns;
		uint64 fiberSleepCost_ns        = 0_ns;
		uint64 fiberReadyRunCount       = 0;
	};

	struct ShardExecutorMetrics
	{
		int32  shardIndex               = -1;

		uint64 loopCount                = 0;
		uint64 didWorkLoopCount         = 0;
		uint64 idleLoopCount            = 0;
		uint64 idleSleepCost_ns         = 0_ns;

		uint64 ingressBatchCount        = 0;
		uint64 ingressJobSubmitCount    = 0;
		uint64 ingressJobCount          = 0;
		uint64 workerJobSubmitCount     = 0;

		uint64 processJobsCallCount     = 0;
		uint64 processJobsExecCount     = 0;

		uint64 mailboxProcessCount						= 0;
		uint64 mailboxJobMoveCount						= 0;
		uint64 mailboxServiceBudgetExhaustedLoopCount	= 0;
		uint64 mailboxTotalJobBudgetHitCount			= 0;
		uint64 mailboxCountBudgetHitCount				= 0;
		uint64 mailboxPerMailboxBudgetHitCount			= 0;
		uint64 mailboxReadyWaitSampleCount				= 0;
		uint64 mailboxReadyWaitTotal_ns					= 0_ns;
		uint64 mailboxReadyWaitMax_ns					= 0_ns;

		uint64 schedulerPollCount       = 0;
		uint64 schedulerEmptyPollCount  = 0;
		uint64 schedulerPollCost_ns     = 0_ns;
		uint64 schedulerReadyRunCount   = 0;

		uint64 tickCount                = 0;
		uint64 tickCatchUpCount         = 0;

		uint64 loopDurationTotal_ns     = 0_ns;
		uint64 loopDurationMax_ns       = 0_ns;
		uint64 jobsExecutedCritical     = 0;
		uint64 jobsExecutedControl      = 0;
		uint64 jobsExecutedBackground   = 0;

		uint64 ingressQueueCurrent      = 0;
		uint64 ingressQueuePeak         = 0;
		uint64 workerQueueCurrent       = 0;
		uint64 workerQueuePeak          = 0;
		uint64 readyMailboxCurrent      = 0;
		uint64 readyMailboxPeak         = 0;
		uint64 readyMailboxSampleCount  = 0;
		uint64 readyMailboxSampleSum    = 0;
		uint64 localCriticalCurrent     = 0;
		uint64 localCriticalPeak        = 0;
		uint64 localControlCurrent      = 0;
		uint64 localControlPeak         = 0;
		uint64 localBackgroundCurrent   = 0;
		uint64 localBackgroundPeak      = 0;
		uint64 localTotalCurrent        = 0;
		uint64 localTotalPeak           = 0;

		uint64 ingressMovedPerLoopMax   = 0;
		uint64 mailboxMovedPerLoopMax   = 0;
		uint64 jobsExecutedPerLoopMax   = 0;

	};

	struct FiberMetrics
	{
		uint64 stepCount                = 0;
		uint64 switchCount              = 0;

		uint64 pollCount                = 0;
		uint64 emptyPollCount           = 0;
		uint64 pollCostAcc_ns           = 0_ns;
		uint64 lastPollCost_ns          = 0_ns;

		uint64 readyRunCount            = 0;
		uint64 lastPollReadyRunCount    = 0;
		uint64 inboxResumeCount         = 0;
		uint64 inboxSpawnCount          = 0;
		uint64 inboxCancelByKeyCount    = 0;
		uint64 inboxCancelByIdCount     = 0;

		uint64 wakeupTimerCount         = 0;
		uint64 wakeupTimeoutCount       = 0;

		uint64 lastStackUsed            = 0;
		uint64 lastStackTotal           = 0;
		uint64 peakStackUsed            = 0;
	};
}
