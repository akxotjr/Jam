#include "pch.h"
#include "jamnet/core/executor/ExecutorTLS.h"

namespace jam
{
	thread_local uint32					tl_ThreadId = 0;
	thread_local std::string_view		tl_ThreadName = "";
	thread_local IExecutor*				tl_Executor = nullptr;
	thread_local std::stack<int32>		tl_LockStack;
}