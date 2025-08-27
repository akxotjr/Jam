#include "pch.h"
//#include "JobTimer.h"
//
//namespace jam::utils::job
//{
//	void JobTimer::Reserve(std::chrono::duration<uint64> after, Wptr<JobQueue> owner, Sptr<Job> job)
//	{
//		const uint64 executeTime = ::GetTickCount64() + std::chrono::duration_cast<std::chrono::milliseconds>(after).count();
//		JobData* jobData = memory::ObjectPool<JobData>::Pop(owner, job);
//
//		WRITE_LOCK
//
//		m_items.push(TimerItem{ .executeTime = executeTime, .jobData = jobData });
//	}
//
//
//	void JobTimer::Distribute(uint64 now)
//	{
//		if (m_distributing.exchange(true) == true)
//			return;
//
//		xvector<TimerItem> items;
//		{
//			WRITE_LOCK
//
//			while (!m_items.empty())
//			{
//				const TimerItem& timerItem = m_items.top();
//
//				if (now < timerItem.executeTime)
//					break;
//
//				items.push_back(timerItem);
//				m_items.pop();
//			}
//		}
//
//		for (TimerItem& item : items)
//		{
//			if (Sptr<JobQueue> owner = item.jobData->owner.lock())
//				owner->Push(item.jobData->job);
//
//			memory::ObjectPool<JobData>::Push(item.jobData);
//		}
//
//		m_distributing.store(false);
//	}
//
//	void JobTimer::Clear()
//	{
//		WRITE_LOCK
//
//		while (!m_items.empty())
//		{
//			const TimerItem& timerItem = m_items.top();
//			memory::ObjectPool<JobData>::Push(timerItem.jobData);
//			m_items.pop();
//		}
//	}
//}
