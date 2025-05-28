#pragma once

namespace jam::utils::thread
{
	class ThreadPool
	{
	public:
		ThreadPool(int32 numThreads, const Function& func);
		~ThreadPool();

		void Run();
		void Join();

		void Stop();
		void Attach();

	private:
		void InitTLS();
		void DestoryTLS();

	private:
		Function				m_function = nullptr;
		std::list<std::thread>	m_threads;
		Atomic<int32>			m_numThreads;
	};
}

