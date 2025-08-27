#pragma once
//#include "Lock.h"
//
//namespace jam::utils::job
//{
//	class Job;
//	class JobQueue;
//	class GlobalQueue;
//
//	struct JobData
//	{
//		JobData(Wptr<JobQueue> owner, Sptr<Job> job) : owner(owner), job(job) {}
//
//		Wptr<JobQueue>					owner;
//		Sptr<Job>						job;
//	};
//
//	struct TimerItem
//	{
//		bool operator<(const TimerItem& other) const
//		{
//			return executeTime > other.executeTime;
//		}
//
//		uint64							executeTime = 0;
//		JobData*						jobData = nullptr;
//	};
//
//
//	class JobTimer
//	{
//	public:
//		void							Reserve(std::chrono::duration<uint64> after, Wptr<JobQueue> owner, Sptr<Job> job);
//		void							Distribute(uint64 now);
//		void							Clear();
//
//	private:
//		USE_LOCK
//		xpqueue<TimerItem>				m_items;
//		Atomic<bool>					m_distributing = false;
//	};
//}
