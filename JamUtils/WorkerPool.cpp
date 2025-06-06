#include "pch.h"
#include "WorkerPool.h"
#include "TimeManager.h"

namespace jam::utils::thrd
{
	WorkerPool::WorkerPool(int32 numThreads, WorkerFactory factory)
	{
		m_numThreads.store(numThreads);

		for (int32 i = 0; i < numThreads; i++)
			m_workers.push_back(factory());
	}

	WorkerPool::~WorkerPool()
	{
	}


	void WorkerPool::Run()
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

	void WorkerPool::Join()
	{
		for (std::thread& t : m_threads)
		{
			if (t.joinable())
				t.join();
		}
		m_threads.clear();
	}

	void WorkerPool::Stop()
	{
	}

	void WorkerPool::Attach()
	{
	}

	job::JobQueue* WorkerPool::GetJobQueueFromAnotherWorker()
	{
		for (auto& worker : m_workers)
		{
			if (auto jobQueue = worker->GetCurrentJobQueue())
			{
				return jobQueue;
			}
		}

		return nullptr;
	}


	void WorkerPool::InitTLS()
	{
		static Atomic<int16> s_threadId = 1;
		tl_ThreadId = s_threadId.fetch_add(1);

		if (tl_Worker == nullptr)
		{
			tl_Worker = m_workers[tl_ThreadId].get();	// todo
		}
	}

	void WorkerPool::DestoryTLS()
	{
	}

	void WorkerPool::Execute()
	{
		while (true)
		{
			tl_Worker->DoBaseJob();

			DistributeReservedJob();

			while (true)
			{
				double now = TimeManager::Instance().GetCurrentTime();
				if (now >= tl_EndTime)
					break;

				tl_Worker->DoJobs();
			}

			int32 workCount = tl_Worker->m_workCount;
			tl_Worker->m_workCount.store(0);

			double nextTime = 0.0;

			if (workCount == 0)
				nextTime = 0.01;
			else if (workCount < 5)
				nextTime = 0.005;
			else
				nextTime = 0.001;

			tl_EndTime = TimeManager::Instance().GetCurrentTime() + nextTime;
			std::this_thread::yield();	// opt
		}
	}

	void WorkerPool::DistributeReservedJob()
	{
		const double now = TimeManager::Instance().GetCurrentTime();

		m_globalQueue->GetJobTimer()->Distribute(now);
	}
}
