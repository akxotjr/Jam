#pragma once

namespace jam
{
    struct GlobalExecutorMetricsSnapshot
    {
        uint64 workerLoopCount          = 0;
        uint64 workerJobExecCount       = 0;
        uint64 workerIdleLoopCount      = 0;
        uint64 workerWaitCost_ns        = 0;
        uint64 workerJobExecCost_ns     = 0;

        uint64 fiberLoopCount           = 0;
        uint64 fiberPollCount           = 0;
        uint64 fiberEmptyPollCount      = 0;
        uint64 fiberPollCost_ns         = 0;
        uint64 fiberSleepCost_ns        = 0;
        uint64 fiberReadyRunCount       = 0;
    };

    struct ShardExecutorMetricsSnapshot
    {
        int32 shardIndex                = -1;

        uint64 loopCount                = 0;
        uint64 didWorkLoopCount         = 0;
        uint64 idleLoopCount            = 0;
        uint64 idleSleepCost_ns         = 0;

        uint64 ingressBatchCount        = 0;
        uint64 ingressJobCount          = 0;

        uint64 processJobsCallCount     = 0;
        uint64 processJobsExecCount     = 0;

        uint64 mailboxProcessCount      = 0;
        uint64 mailboxJobMoveCount      = 0;

        uint64 schedulerPollCount       = 0;
        uint64 schedulerEmptyPollCount  = 0;
        uint64 schedulerPollCost_ns     = 0;
        uint64 schedulerReadyRunCount   = 0;

        uint64 tickCount                = 0;
        uint64 tickCatchUpCount         = 0;
    };
}
