#pragma once
#include "GlobalQueue.h"


namespace jam::utils::thrd
{
	class Worker;
	class Scheduler;
	
	class ThreadPool
	{
		friend class Worker;

		using WorkerFactory = std::function<Uptr<Worker>()>;

	public:
		ThreadPool(int32 numThreads, WorkerFactory factory);
		~ThreadPool();

		void Run();
		void Join();

		void Stop();
		void Attach();

	private:
		void InitTLS();
		void DestoryTLS();
		void Execute();

	private:
		std::list<std::thread>					m_threads;
		Atomic<int32>							m_numThreads;

		std::vector<std::unique_ptr<Worker>>	m_workers;

		Uptr<job::GlobalQueue>					m_globalQueue;
	};
}

