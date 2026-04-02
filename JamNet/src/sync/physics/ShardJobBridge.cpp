#include "pch.h"
#include "jamnet/sync/physics/ShardJobBridge.h"


namespace jam::net
{
	ShardJobBridge::ShardJobBridge(ShardExecutor& executor)
		: m_executor(executor)
	{
	}

	void ShardJobBridge::SubmitJob(std::function<void()> fn)
	{
		m_executor.Submit(Job(std::move(fn)));
	}

	void ShardJobBridge::NotifyComplete(uint64_t awaitKey)
	{
		m_executor.ResumeFiber(static_cast<FiberAwaitKey>(awaitKey));
	}

	bool ShardJobBridge::IsInFiberContext() const
	{
		const auto* shard = SHARD_LOCAL_CURRENT();
		return shard && shard->scheduler && (shard->scheduler->Current() != 0);
	}
}
