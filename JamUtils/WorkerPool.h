#pragma once
#include "GlobalQueue.h"


namespace jam::utils::thrd
{
	class Worker;
	class Scheduler;
	
	class WorkerPool
	{
		friend class Worker;

		using WorkerFactory = std::function<Uptr<Worker>()>;

	public:
		WorkerPool(int32 numWorkers, WorkerFactory factory);
		~WorkerPool();

		void								Run();
		void								Join();

		void								Stop();
		void								Attach();

		//void								SetGlobalQueue(Uptr<job::GlobalQueue> gq);
		job::GlobalQueue*					GetGlobalQueue() { return m_globalQueue.get(); }

		job::JobQueue*						GetJobQueueFromAnotherWorker();

	private:
		void								InitTLS();
		void								DestoryTLS();

		void								Execute();
		void								DistributeReservedJob();

	private:
		xlist<std::thread>					m_threads;
		Atomic<int32>						m_numWorkers;

		xvector<Uptr<Worker>>				m_workers;

		Uptr<job::GlobalQueue>				m_globalQueue;
	};
}

