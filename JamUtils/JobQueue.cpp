#include "pch.h"
#include "JobQueue.h"

namespace jam::utils::job
{
	void JobQueue::Push(JobRef job, bool pushOnly)
	{
		const int32 prevCount = _jobCount.fetch_add(1);
		_jobs.Push(job);

		if (prevCount == 0)
		{
			if (thread::LCurrentJobQueue == nullptr && pushOnly == false)
			{
				Execute();
			}
			else
			{
				GlobalQueue::Instance().Push(shared_from_this());
			}
		}
	}



	void JobQueue::Execute()
	{
		thread::LCurrentJobQueue = this;

		while (true)
		{
			Vector<JobRef> jobs;
			_jobs.PopAll(OUT jobs);

			const int32 jobCount = static_cast<int32>(jobs.size());

			for (int32 i = 0; i < jobCount; i++)
				jobs[i]->Execute();

			if (_jobCount.fetch_sub(jobCount) == jobCount)
			{
				thread::LCurrentJobQueue = nullptr;
				return;
			}

			const double now = TimeManager::Instance().GetServerTime();
			if (now >= thread::LEndTime)
			{
				thread::LCurrentJobQueue = nullptr;

				GlobalQueue::Instance().Push(shared_from_this());
				break;
			}
		}
	}
}
