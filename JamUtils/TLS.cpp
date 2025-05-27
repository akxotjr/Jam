#include "pch.h"
#include "TLS.h"

namespace jam::utils::thread
{
	thread_local uint32									tl_ThreadId = 0;
	thread_local double									tl_EndTime = 0.0;
	thread_local std::stack<int32>						tl_LockStack;
	//thread_local network::SendBufferChunkRef		LSendBufferChunk;
	thread_local jam::utils::job::JobQueue*				tl_CurrentJobQueue = nullptr;
}