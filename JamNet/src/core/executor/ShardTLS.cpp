#include "pch.h"
#include "jamnet/core/executor/ShardTLS.h"

namespace jam
{
	thread_local ShardTLS::ThreadData ShardTLS::tl_threadData;


	void ShardTLS::Bind(ShardLocal* L, std::thread::id tid)
	{
		if (!L)
			throw std::invalid_argument("ShardLocal cannot be null");
		if (tl_threadData.bound)
			throw std::runtime_error("Thread already bound to a shard");

		tl_threadData.local				= L;
		tl_threadData.expectedThreadId	= tid;
		tl_threadData.bound				= true;

		if (std::this_thread::get_id() != tid) 
		{
			tl_threadData.bound = false;
			tl_threadData.local = nullptr;
			tl_threadData.expectedThreadId = std::thread::id{};
			throw std::runtime_error("Thread ID mismatch during binding");
		}
	}

	void ShardTLS::Unbind()
	{
		tl_threadData.local = nullptr;
		tl_threadData.expectedThreadId = std::thread::id{};
		tl_threadData.bound = false;
	}

	ShardLocal* ShardTLS::GetCurrent()
	{
		if (!ValidateAccess()) return nullptr;
		return tl_threadData.local;
	}

	ShardLocal& ShardTLS::GetCurrentChecked()
	{
		if (!ValidateAccess()) ThrowInvalidAccess();
		return *tl_threadData.local;
	}

	bool ShardTLS::IsShardThread()
	{
		return ValidateAccess();
	}

	std::thread::id ShardTLS::GetBoundThreadId()
	{
		return tl_threadData.expectedThreadId;
	}

	bool ShardTLS::ValidateAccess()
	{
		if (!tl_threadData.bound || !tl_threadData.local) 
			return false;

		if (std::this_thread::get_id() != tl_threadData.expectedThreadId) 
			return false;

		return true;
	}

	void ShardTLS::ThrowInvalidAccess()
	{
		if (!tl_threadData.bound)
			throw std::runtime_error("No shard bound to current thread");
		if (std::this_thread::get_id() != tl_threadData.expectedThreadId)
			throw std::runtime_error("Invalid cross-thread access detected");
		throw std::runtime_error("Invalid shard access");
	}
}
