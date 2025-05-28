#include "pch.h"
#include "JobTimer.h"
#include "TimeManager.h"

namespace jam::utils::job
{
	void JobTimer::Init()
	{
		ISingletonLayer::Init();
	}

	void JobTimer::Shutdown()
	{
		ISingletonLayer::Shutdown();
	}


	void JobTimer::Reserve(double afterTime, std::weak_ptr<JobQueue> owner, JobRef job)
	{
		const double executeTime = TimeManager::Instance()->GetServerTime() + afterTime;
		JobData* jobData = memory::ObjectPool<JobData>::Pop(owner, job);

		WRITE_LOCK

		m_items.push(TimerItem{ .executeTime = executeTime, .jobData = jobData });
	}


	void JobTimer::Distribute(double now)
	{
		if (m_distributing.exchange(true) == true)
			return;

		xvector<TimerItem> items;
		{
			WRITE_LOCK

			while (!m_items.empty())
			{
				const TimerItem& timerItem = m_items.top();

				if (now < timerItem.executeTime)
					break;

				items.push_back(timerItem);
				m_items.pop();
			}
		}

		for (TimerItem& item : items)
		{
			if (JobQueueRef owner = item.jobData->owner.lock())
				owner->Push(item.jobData->job);

			memory::ObjectPool<JobData>::Push(item.jobData);
		}

		m_distributing.store(false);
	}

	void JobTimer::Clear()
	{
		WRITE_LOCK

		while (!m_items.empty())
		{
			const TimerItem& timerItem = m_items.top();
			memory::ObjectPool<JobData>::Push(timerItem.jobData);
			m_items.pop();
		}
	}
}
