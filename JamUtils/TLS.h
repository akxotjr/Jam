#pragma once
#include <stack>
#include "Worker.h"

namespace jam::utils::thrd
{
	extern thread_local uint32										tl_ThreadId;
	extern thread_local uint64										tl_EndTime;
	extern thread_local std::stack<int32>							tl_LockStack;
	//extern thread_local Worker*										tl_Worker;
}
