#include "pch.h"
#include "JobQueue.h"

#include "GlobalQueue.h"
#include "TimeManager.h"

namespace jam::utils::job
{
	void JobQueue::Push(JobRef job, bool pushOnly)
	{
		const int32 prevCount = m_jobCount.fetch_add(1);
		m_jobs.Push(job);

		if (prevCount == 0)
		{
			if (thread::tl_CurrentJobQueue == nullptr && pushOnly == false)
			{
				Execute();
			}
			else
			{
				GlobalQueue::Instance()->Push(shared_from_this());
			}
		}
	}



	void JobQueue::Execute()
	{
		thread::tl_CurrentJobQueue = this;

		while (true)
		{
			xvector<JobRef> jobs;
			m_jobs.PopAll(OUT jobs);

			const int32 jobCount = static_cast<int32>(jobs.size());

			for (int32 i = 0; i < jobCount; i++)
				jobs[i]->Execute();

			if (m_jobCount.fetch_sub(jobCount) == jobCount)
			{
				thread::tl_CurrentJobQueue = nullptr;
				return;
			}

			const double now = TimeManager::Instance()->GetServerTime();
			if (now >= thread::tl_EndTime)
			{
				thread::tl_CurrentJobQueue = nullptr;

				GlobalQueue::Instance()->Push(shared_from_this());
				break;
			}
		}
	}
}
