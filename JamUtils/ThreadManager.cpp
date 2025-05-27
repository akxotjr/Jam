#include "pch.h"
#include "ThreadManager.h"
#include "GlobalQueue.h"
#include "JobTimer.h"
#include "TimeManager.h"

namespace jam::utils::thread
{
	void ThreadManager::Init()
	{
		ISingletonLayer::Init();
		InitTLS();
	}

	void ThreadManager::Shutdown()
	{
		Join();
		ISingletonLayer::Shutdown();
	}

	void ThreadManager::Launch(std::function<void(void)> callback)
	{
		LockGuard guard(m_lock);

		m_threads.push_back(std::thread([this, callback]()
			{
				InitTLS();
				callback();
				DestroyTLS();
			}));
	}

	void ThreadManager::Join()
	{
		for (std::thread& t : m_threads)
		{
			if (t.joinable())
				t.join();
		}
		m_threads.clear();
	}

	void ThreadManager::InitTLS()
	{
		static Atomic<uint32> s_threadId = 1;
		tl_ThreadId = s_threadId.fetch_add(1);
	}

	void ThreadManager::DestroyTLS()
	{
	}

	int32 ThreadManager::DoGlobalQueueWork()
	{
		int32 workCount = 0;

		while (true)
		{
			double now = TimeManager::Instance()->GetServerTime();
			if (now > tl_EndTime)
				break;

			job::JobQueueRef jobQueue = job::GlobalQueue::Instance()->Pop();
			if (jobQueue == nullptr)
				break;

			jobQueue->Execute();
			++workCount;
		}
		return workCount;
	}

	void ThreadManager::DistributeReservedJob()
	{
		const double now = TimeManager::Instance()->GetServerTime();

		job::JobTimer::Instance()->Distribute(now);
	}
}
