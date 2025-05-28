#include "pch.h"
#include "ThreadPool.h"

namespace jam::utils::thread
{
	ThreadPool::ThreadPool(int32 numThreads, const Function& func)
	{
		m_numThreads.store(numThreads);
		m_function = func;
	}

	ThreadPool::~ThreadPool()
	{
	}


	void ThreadPool::Run()
	{
		if (m_function == nullptr)
			return;

		for (int32 i = 0; i < m_numThreads; i++)
		{
			m_threads.push_back(std::thread([this]()
				{
					InitTLS();
					m_function();
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
	}

	void ThreadPool::DestoryTLS()
	{
	}


}
