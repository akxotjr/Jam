#include "pch.h"
#include "GlobalQueue.h"

namespace jam::utils::job
{
	void GlobalQueue::Push(const JobQueueRef& jobQueue)
	{
		m_jobQueues.Push(jobQueue);
	}

	JobQueueRef GlobalQueue::Pop()
	{
		return m_jobQueues.Pop();
	}
}
