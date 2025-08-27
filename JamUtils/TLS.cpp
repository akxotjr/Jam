#include "pch.h"
#include "TLS.h"

namespace jam::utils::thrd
{
	thread_local uint32									tl_ThreadId = 0;
	thread_local uint64									tl_EndTime = 0;
	thread_local std::stack<int32>						tl_LockStack;
	//thread_local Worker*								tl_Worker = nullptr;
}