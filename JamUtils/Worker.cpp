#include "pch.h"
#include "Worker.h"

#include "TimeManager.h"

namespace jam::utils::thrd
{
	Worker::Worker()
	{
	}

	int32 Worker::Execute()
	{
		int32 workCount = 0;

		const double now = TimeManager::Instance()->GetCurrentTime();
		if (now >= tl_EndTime)
			return workCount;

		if (m_jobQueue == nullptr)
			m_jobQueue = m_owner.lock()->m_globalQueue->Pop().get();

		while (true)
		{
			double nowInner = TimeManager::Instance()->GetCurrentTime();
			if (nowInner >= tl_EndTime)
				break;

			m_jobQueue->Execute();
			++workCount;
		}

		m_jobQueue = nullptr;

		return workCount;
	}
}
