#pragma once
#include "Lock.h"

namespace jam::utils::job
{
	class JobQueue;

	struct JobData
	{
		JobData(std::weak_ptr<JobQueue> owner, JobRef job) : owner(owner), job(job) {}

		std::weak_ptr<JobQueue>			owner;
		JobRef							job;
	};

	struct TimerItem
	{
		bool operator<(const TimerItem& other) const
		{
			return executeTime > other.executeTime;
		}

		double							executeTime = 0;
		JobData* jobData = nullptr;
	};


	class JobTimer
	{
		DECLARE_SINGLETON(JobTimer)

	public:
		void							Reserve(double afterTime, std::weak_ptr<JobQueue> owner, JobRef job);
		void							Distribute(double now);
		void							Clear();

	private:
		USE_LOCK
		PriorityQueue<TimerItem>		_items;
		Atomic<bool>					_distributing = false;
	};
}
