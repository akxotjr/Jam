#pragma once
#include <stack>
#include "IExecutor.h"

namespace jam
{
	extern thread_local uint32				tl_ThreadId;
	extern thread_local string_view			tl_ThreadName;
	extern thread_local IExecutor*			tl_Executor;

	extern thread_local stack<int32>		tl_LockStack;
}
