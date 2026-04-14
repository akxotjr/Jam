#pragma once
#include "jamnet/core/executor/IExecutor.h"

#include <stack>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace jam
{
	struct ShardLocal;

	struct ShardThreadContext
	{
		ShardLocal*			local				= nullptr;
		std::thread::id		expectedThreadId	= {};
		bool				bound				= false;
	};

	struct ThreadContext
	{
		uint32				threadId			= 0;
		std::string			threadName;
		IExecutor*			executor			= nullptr;

		std::stack<int32>	lockStack;
		ShardThreadContext	shard				= {};
	};

	extern thread_local ThreadContext		tl_ThreadContext;


	ThreadContext&				CurrentThreadContext();


	inline void InitThreadContext(const std::string& name, IExecutor* executor)
	{
		static std::atomic<uint32> g_nextThreadId = 1;

		auto& ctx = CurrentThreadContext();
		ctx = ThreadContext{
			.threadId	= g_nextThreadId.fetch_add(1, std::memory_order_relaxed),
			.threadName = name,
			.executor	= executor
		};
	}

	inline uint32				CurrentThreadId()   { return CurrentThreadContext().threadId; }
	inline std::string_view		CurrentThreadName() { return std::string_view(CurrentThreadContext().threadName); }
	inline IExecutor*			CurrentExecutor()   { return CurrentThreadContext().executor; }

	inline void BindShardContext(ShardLocal* L, std::thread::id tid)
	{
		if (!L)
			throw std::invalid_argument("ShardLocal cannot be null");

		auto& shardCtx = CurrentThreadContext().shard;
		if (shardCtx.bound)
			throw std::runtime_error("Thread already bound to a shard");

		shardCtx.local				= L;
		shardCtx.expectedThreadId	= tid;
		shardCtx.bound				= true;

		if (std::this_thread::get_id() != tid)
		{
			shardCtx = {};
			throw std::runtime_error("Thread ID mismatch during binding");
		}
	}

	inline void UnbindShardContext()
	{
		CurrentThreadContext().shard = {};
	}

	inline bool IsShardThread()
	{
		const auto& shardCtx = CurrentThreadContext().shard;
		return shardCtx.bound && shardCtx.local && std::this_thread::get_id() == shardCtx.expectedThreadId;
	}

	inline ShardLocal* CurrentShardLocal()
	{
		return IsShardThread() ? CurrentThreadContext().shard.local : nullptr;
	}

	inline ShardLocal& CurrentShardLocalChecked()
	{
		if (auto* local = CurrentShardLocal())
			return *local;

		const auto& shardCtx = CurrentThreadContext().shard;
		if (!shardCtx.bound)
			throw std::runtime_error("No shard bound to current thread");
		if (std::this_thread::get_id() != shardCtx.expectedThreadId)
			throw std::runtime_error("Invalid cross-thread access detected");
		throw std::runtime_error("Invalid shard access");
	}

	inline std::thread::id CurrentShardBoundThreadId()
	{
		return CurrentThreadContext().shard.expectedThreadId;
	}
}
