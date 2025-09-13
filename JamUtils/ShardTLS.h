#pragma once


namespace jam::utils::exec
{
	struct ShardLocal;
	class ShardExecutor;

	class ShardTLS
	{
	public:

		static void				Bind(ShardLocal* L, std::thread::id tid);
		static void				Unbind();

		static ShardLocal*		GetCurrent();
		static ShardLocal&		GetCurrentChecked();

		static bool				IsShardThread();

		static std::thread::id	GetBoundThreadId();

	private:

		static bool				ValidateAccess();
		static void				ThrowInvalidAccess();


		struct ThreadData
		{
			ShardLocal*			local = nullptr;
			std::thread::id		expectedThreadId{};
			bool				bound = false;
		};

		static thread_local ThreadData tl_threadData;
	};
}


#define SHARD_LOCAL_CURRENT()       jam::utils::exec::ShardTLS::GetCurrent()
#define SHARD_LOCAL_CHECKED()       jam::utils::exec::ShardTLS::GetCurrentChecked()
#define IS_SHARD_THREAD()           jam::utils::exec::ShardTLS::IsShardThread()
