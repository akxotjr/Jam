#include "pch.h"
#include "jamnet/core/executor/ThreadContext.h"

namespace jam
{
	thread_local ThreadContext tl_ThreadContext;

	ThreadContext& CurrentThreadContext()
	{
		return tl_ThreadContext;
	}
}
