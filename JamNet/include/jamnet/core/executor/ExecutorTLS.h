#pragma once
#include <stack>
#include "jamnet/core/executor/IExecutor.h"

namespace jam
{
	extern thread_local uint32				tl_ThreadId;
	extern thread_local std::string_view	tl_ThreadName;
	extern thread_local IExecutor*			tl_Executor;

	extern thread_local std::stack<int32>	tl_LockStack;
}
