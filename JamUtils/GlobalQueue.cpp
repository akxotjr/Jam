#include "pch.h"
#include "GlobalQueue.h"
#include "JobTimer.h"

namespace jam::utils::job
{
	GlobalQueue::GlobalQueue()
	{
		m_jobTimer = make_unique<JobTimer>();
	}

	void GlobalQueue::Push(const JobQueueRef& jobQueue)
	{
		m_jobQueues.Push(jobQueue);
	}

	JobQueueRef GlobalQueue::Pop()
	{
		return m_jobQueues.Pop();
	}
}
