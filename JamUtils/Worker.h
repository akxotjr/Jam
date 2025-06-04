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
		virtual ~Worker() = default;

		void SetWork(Function work) { m_work = work; }
		void						Execute();
		job::JobQueue*				GetCurrentJobQueue() const { return m_currentJobQueue; }

	private:
		void						Steal();

	private:
		Function					m_work = nullptr;	// temp

		Atomic<int32>				m_workCount = 0;
		Wptr<ThreadPool>			m_owner;
		job::JobQueue*				m_currentJobQueue = nullptr;
	};
}

