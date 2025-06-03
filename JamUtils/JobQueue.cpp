#include "pch.h"
#include "JobQueue.h"
#include "GlobalQueue.h"
#include "TimeManager.h"
#include "Worker.h"

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
				ExecuteFront();
			}
			else
			{
				m_owner.lock()->Push(shared_from_this());
			}
		}
	}

	void JobQueue::ExecuteFront()
	{
		while (true)
		{
			Sptr<Job> job = m_jobs.PopFront();
			if (job == nullptr)
				break;

			job->Execute();
			thrd::tl_Worker->m_workCount.fetch_add(1);

			const double now = TimeManager::Instance().GetCurrentTime();
			if (now >= thrd::tl_EndTime)
			{
				m_owner.lock()->Push(shared_from_this());
				break;
			}
		}
	}

	void JobQueue::ExecuteBack()
	{
		while (true)
		{
			Sptr<Job> job = m_jobs.PopBack();
			if (job == nullptr)
				break;

			job->Execute();
			thrd::tl_Worker->m_workCount.fetch_add(1);

			const double now = TimeManager::Instance().GetCurrentTime();
			if (now >= thrd::tl_EndTime)
			{
				m_owner.lock()->Push(shared_from_this());
				break;
			}
		}
	}
}
