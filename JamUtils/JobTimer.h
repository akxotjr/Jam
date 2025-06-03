#pragma once
#include "Lock.h"

namespace jam::utils::job
{
	class Job;
	class JobQueue;
	class GlobalQueue;

	struct JobData
	{
		JobData(Wptr<JobQueue> owner, JobRef job) : owner(owner), job(job) {}

		Wptr<JobQueue>					owner;
		JobRef							job;
	};

	struct TimerItem
	{
		bool operator<(const TimerItem& other) const
		{
			return executeTime > other.executeTime;
		}

		double							executeTime = 0;
		JobData*						jobData = nullptr;
	};


	class JobTimer
	{
	public:
		void							Reserve(double afterTime, Wptr<JobQueue> owner, JobRef job);
		void							Distribute(double now);
		void							Clear();

	private:
		USE_LOCK
		xpqueue<TimerItem>				m_items;
		Atomic<bool>					m_distributing = false;
	};
}
