#pragma once
#include "concurrentqueue/blockingconcurrentqueue.h"
#include "concurrentqueue/concurrentqueue.h"

namespace jam::utils::exec
{
	struct GlobalExecutorConfig
	{
		int32 workers = std::thread::hardware_concurrency();
		uint64 capacity = 1 << 16;
	};


	class GlobalExecutor
	{
	public:
		GlobalExecutor(const GlobalExecutorConfig& config = {});
		~GlobalExecutor();

		void Post();
		void PostAfter();
		void Stop();

	private:
		Atomic<bool> m_running;
		moodycamel::ConcurrentQueue<job::Job> m_queue;
		xvector<std::thread> m_workers;
	};

}
