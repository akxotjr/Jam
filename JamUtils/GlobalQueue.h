#pragma once
#include "ISingletonLayer.h"

namespace jam::utils::job
{
	class GlobalQueue : public ISingletonLayer<GlobalQueue>
	{
		friend class jam::ISingletonLayer<GlobalQueue>;

	public:
		void							Push(const JobQueueRef& jobQueue);
		JobQueueRef						Pop();

	private:
		thread::LockQueue<JobQueueRef>	m_jobQueues;
	};
}
