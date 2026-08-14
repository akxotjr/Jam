#include "pch.h"

#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/core/executor/ShardExecutor.h"

#include "jamnet/runtime/world/simulation/common/ShardJobBridge.h"


namespace jam::net
{
	ShardJobBridge::ShardJobBridge(ShardExecutor& executor)
		: m_executor(executor)
	{
	}

	void ShardJobBridge::SubmitJob(std::function<void()> fn)
	{
		m_executor.SubmitWorkerJob(Job(std::move(fn)));
	}

	void ShardJobBridge::NotifyComplete(uint64_t awaitKey)
	{
		constexpr int32 kPhysicsResumePriority = -1;
		m_executor.ResumeFiber(static_cast<FiberAwaitKey>(awaitKey), kPhysicsResumePriority);
	}

	bool ShardJobBridge::IsInFiberContext() const
	{
		const auto* shard = CurrentShardLocal();
		return shard && shard->scheduler && (shard->scheduler->Current() != 0);
	}

}
