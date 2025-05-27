#include "pch.h"
#include "JobTimer.h"

namespace jam::utils::job
{
	void JobTimer::Reserve(double afterTime, std::weak_ptr<JobQueue> owner, JobRef job)
	{
		const double executeTime = TimeManager::Instance().GetServerTime() + afterTime;
		JobData* jobData = memory::ObjectPool<JobData>::Pop(owner, job);

		WRITE_LOCK

			_items.push(TimerItem{ .executeTime = executeTime, .jobData = jobData });
	}


	void JobTimer::Distribute(double now)
	{
		if (_distributing.exchange(true) == true)
			return;

		Vector<TimerItem> items;
		{
			WRITE_LOCK

				while (!_items.empty())
				{
					const TimerItem& timerItem = _items.top();

					if (now < timerItem.executeTime)
						break;

					items.push_back(timerItem);
					_items.pop();
				}
		}

		for (TimerItem& item : items)
		{
			if (JobQueueRef owner = item.jobData->owner.lock())
				owner->Push(item.jobData->job);

			memory::ObjectPool<JobData>::Push(item.jobData);
		}

		_distributing.store(false);
	}

	void JobTimer::Clear()
	{
		WRITE_LOCK

			while (!_items.empty())
			{
				const TimerItem& timerItem = _items.top();
				memory::ObjectPool<JobData>::Push(timerItem.jobData);
				_items.pop();
			}
	}
}
