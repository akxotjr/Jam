#pragma once
#include "ThreadPool.h"

namespace jam::utils::thrd
{
	class Worker
	{
	public:
		Worker();
		~Worker() = default;

		int32						Execute();
		job::JobQueue*				GetCurrentJobQueue() const { return m_jobQueue; }

	private:
		Wptr<ThreadPool>			m_owner;
		job::JobQueue*				m_jobQueue = nullptr;
	};

}

