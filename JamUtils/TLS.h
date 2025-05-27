#pragma once

#include <stack>

#include "JobQueue.h"

namespace jam::utils::thread
{
	extern thread_local uint32										tl_ThreadId;
	extern thread_local double										tl_EndTime;
	extern thread_local std::stack<int32>							tl_LockStack;
	//extern thread_local network::SendBufferChunkRef					LSendBufferChunk;
	extern thread_local job::JobQueue*								tl_CurrentJobQueue;
}
