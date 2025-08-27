#pragma once
//#include "JobQueue.h"
//
//namespace jam::utils::thrd
//{
//	class WorkerPool;
//	class Fiber;
//	class FiberScheduler;
//	class job::JobQueue;
//
//	using FiberPool = ObjectPool<Fiber>;
//
//	class Worker
//	{
//		friend class WorkerPool;
//		friend class job::JobQueue;
//
//	public:
//		Worker();
//		virtual ~Worker() = default;
//
//		void						Init();
//
//		void						Run();
//		void*						GetMainFiber() const { return m_mainFiber; }
//		FiberScheduler*				GetScheduler() const { return m_scheduler.get(); }
//
//		void						SetBaseJob(const job::Job& job);
//		void						DoBaseJob();
//		void						DoJobs();
//		job::JobQueue*				GetCurrentJobQueue() const { return m_currentJobQueue; }
//
//		void						AdjustSleepInterval();
//
//	private:
//		void						Steal();
//
//	private:
//		Uptr<job::Job>				m_baseJob = nullptr;
//
//		Atomic<int32>				m_workCount = 0;
//		Wptr<WorkerPool>			m_owner;
//		job::JobQueue*				m_currentJobQueue = nullptr;
//
//		bool						m_steal = false;
//
//		Uptr<FiberScheduler>		m_scheduler = nullptr;
//		void*						m_mainFiber = nullptr;
//	};
//}

