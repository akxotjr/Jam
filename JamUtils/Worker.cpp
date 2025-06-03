#include "pch.h"
#include "Worker.h"
#include "ThreadPool.h"
#include "TimeManager.h"

namespace jam::utils::thrd
{
	Worker::Worker()
	{
	}

	void Worker::Execute()
	{
		const double now = TimeManager::Instance().GetCurrentTime();
		if (now >= tl_EndTime)
		{
			return;
		}

		if (m_jobQueue == nullptr)
			m_jobQueue = m_owner.lock()->m_globalQueue->Pop().get();

		while (true)
		{
			double nowInner = TimeManager::Instance().GetCurrentTime();
			if (nowInner >= tl_EndTime)
				break;

			m_jobQueue->ExecuteFront();
		}

		m_jobQueue = nullptr;

		Steal();
	}

	void Worker::Steal()
	{
		const double now = TimeManager::Instance().GetCurrentTime();

		if (now >= tl_EndTime)
			return;

		job::JobQueue* jobQueue = m_owner.lock()->GetJobQueueFromAnotherWokrer();
		jobQueue->ExecuteBack();
	}
}
