#pragma once

namespace jam::utils::thrd
{
	class ThreadPool;

	class Worker
	{
		friend class ThreadPool;
		friend class job::JobQueue;

	public:
		Worker();
		~Worker() = default;

		void						Execute();
		job::JobQueue*				GetCurrentJobQueue() const { return m_jobQueue; }

	private:
		void Steal();

	private:
		Atomic<int32>				m_workCount = 0;
		Wptr<ThreadPool>			m_owner;
		job::JobQueue*				m_jobQueue = nullptr;
	};
}

