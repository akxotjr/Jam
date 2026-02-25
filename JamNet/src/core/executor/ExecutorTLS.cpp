#include "pch.h"
#include "jamnet/core/executor/ExecutorTLS.h"

namespace jam
{
	thread_local uint32				tl_ThreadId = 0;
	thread_local string_view		tl_ThreadName = "";
	thread_local IExecutor*			tl_Executor = nullptr;
	//thread_local uint64				tl_EndTime = 0;
	thread_local stack<int32>		tl_LockStack;
}