#pragma once
//#include "LockQueue.h"
//
//
//namespace jam::utils::job
//{
//	class ThreadPool;
//	class JobTimer;
//
//	class GlobalQueue
//	{
//		friend class JobQueue;
//
//	public:
//		GlobalQueue();
//		~GlobalQueue() = default;
//
//		void								Push(const Sptr<JobQueue>& jobQueue);
//		Sptr<JobQueue>						Pop();
//
//		JobTimer*							GetJobTimer() { return m_jobTimer.get(); }
//
//	private:
//		thrd::LockQueue<Sptr<JobQueue>>		m_jobQueues;
//		Uptr<JobTimer>						m_jobTimer;
//	};
//}
