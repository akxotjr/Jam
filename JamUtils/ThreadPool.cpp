#include "pch.h"
#include "ThreadPool.h"

#include "TimeManager.h"

namespace jam::utils::thrd
{
	ThreadPool::ThreadPool(int32 numThreads, WorkerFactory factory)
	{
		m_numThreads.store(numThreads);

		for (int32 i = 0; i < numThreads; i++)
			m_workers.push_back(factory());
	}

	ThreadPool::~ThreadPool()
	{
	}


	void ThreadPool::Run()
	{
		for (int32 i = 0; i < m_numThreads; i++)
		{
			m_threads.push_back(std::thread([this]()
				{
					InitTLS();
					Execute();
					DestoryTLS();
				}));
		}
	}

	void ThreadPool::Join()
	{
		for (std::thread& t : m_threads)
		{
			if (t.joinable())
				t.join();
		}
		m_threads.clear();
	}

	void ThreadPool::Stop()
	{
	}

	void ThreadPool::Attach()
	{
	}


	void ThreadPool::InitTLS()
	{
		static Atomic<int16> s_threadId = 1;
		tl_ThreadId = s_threadId.fetch_add(1);

		if (tl_Worker == nullptr)
		{
			tl_Worker = m_workers[tl_ThreadId].get();	// todo
		}
	}

	void ThreadPool::DestoryTLS()
	{
	}

	void ThreadPool::Execute()
	{
		while (true)
		{
			//DistributeReservedJob();
			//int32 workCount = DoGlobalQueueWork();

			int32 workCount = 0;

			while (true)
			{
				double now = TimeManager::Instance()->GetCurrentTime();
				if (now >= tl_EndTime)
					break;

				workCount += tl_Worker->Execute();
			}


			double nextTime = 0.0;

			if (workCount == 0)
				nextTime = 0.01;
			else if (workCount < 5)
				nextTime = 0.005;
			else
				nextTime = 0.001;

			tl_EndTime = TimeManager::Instance()->GetCurrentTime() + nextTime;
			std::this_thread::yield();	// opt
		}
	}


}
