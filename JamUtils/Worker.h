#pragma once

namespace jam::utils::thrd
{
	class WorkerPool;

	class Worker
	{
		friend class WorkerPool;
		friend class job::JobQueue;

	public:
		Worker();
		virtual ~Worker() = default;

		void						SetBaseJob(const job::Job& job);
		void						DoBaseJob();
		void						DoJobs();
		job::JobQueue*				GetCurrentJobQueue() const { return m_currentJobQueue; }

	private:
		void						Steal();

	private:
		Uptr<job::Job>				m_baseJob = nullptr;

		Atomic<int32>				m_workCount = 0;
		Wptr<WorkerPool>			m_owner;
		job::JobQueue*				m_currentJobQueue = nullptr;

		bool						m_steal = false;
	};
}

