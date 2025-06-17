#include "pch.h"
#include "WorkerPool.h"

namespace jam::utils::thrd
{
	WorkerPool::WorkerPool(int32 numWorkers, WorkerFactory factory)
	{
		m_numWorkers.store(numWorkers);

		for (int32 i = 0; i < numWorkers; i++)
			m_workers.push_back(factory());

		m_globalQueue = make_unique<job::GlobalQueue>();
	}

	WorkerPool::~WorkerPool()
	{
	}


	void WorkerPool::Run()
	{
		for (int32 i = 0; i < m_numWorkers; i++)
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
			tl_Worker = m_workers[tl_ThreadId % m_numWorkers].get();	// todo
		}
	}

	void WorkerPool::DestoryTLS()
	{
	}

	void WorkerPool::Execute()
	{
		tl_Worker->Init();
		tl_Worker->Run();
	}

	void WorkerPool::DistributeReservedJob()
	{
		const uint64 now = ::GetTickCount64();

		m_globalQueue->GetJobTimer()->Distribute(now);
	}
}
