#include "pch.h"
#include "jamnet/core/executor/ThreadRegistry.h"
#include "jamnet/core/executor/ExecutorTLS.h"


namespace jam
{
	uint32 ThreadRegistry::AllocateThreadID()
	{
		static std::atomic<uint32> g_nextThreadId{ 1 };
		return g_nextThreadId.fetch_add(1, std::memory_order_relaxed);
	}

	void ThreadRegistry::InitExecutorThread(std::string_view name, IExecutor* executor)
	{
		tl_ThreadId = AllocateThreadID();
		tl_ThreadName = name;
		tl_Executor = executor;
	}

	std::string ThreadRegistry::GetCurrentThreadInfo()
	{
		if (tl_ThreadId == 0)
			return "UnknownThread";

		return std::format("{}[ID={}]", tl_ThreadName, tl_ThreadId);
	}
}
