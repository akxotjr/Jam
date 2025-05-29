#include "pch.h"
#include "JobQueue.h"
#include "GlobalQueue.h"
#include "TimeManager.h"

namespace jam::utils::job
{
	JobQueue::JobQueue(Sptr<GlobalQueue> owner)
	{
		m_owner = owner;
	}

	void JobQueue::Push(JobRef job, bool pushOnly)
	{
		const int32 prevCount = m_jobCount.fetch_add(1);
		m_jobs.PushBack(job);

		if (prevCount == 0)
		{
			if (thrd::tl_Worker != nullptr && thrd::tl_Worker->GetCurrentJobQueue() == nullptr && pushOnly == false)
			{
				Execute();
			}
			else
			{
				m_owner.lock()->Push(shared_from_this());
			}
		}
	}

	void JobQueue::Execute()
	{
		while (true)
		{
			xvector<JobRef> jobs;
			m_jobs.PopAll(OUT jobs);

			const int32 jobCount = static_cast<int32>(jobs.size());

			for (int32 i = 0; i < jobCount; i++)
				jobs[i]->Execute();

			if (m_jobCount.fetch_sub(jobCount) == jobCount)
			{
				return;
			}

			const double now = TimeManager::Instance()->GetCurrentTime();
			if (now >= thrd::tl_EndTime)
			{
				m_owner.lock()->Push(shared_from_this());
				break;
			}
		}
	}
}
