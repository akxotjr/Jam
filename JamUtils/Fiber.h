#pragma once

namespace jam::utils::thrd
{
	class Fiber
	{
		friend class FiberScheduler;

	public:
		Fiber();
		~Fiber();


		void		BindJob(Sptr<job::Job> job, void* mainFiber);
		void		SwitchTo();
		void		YieldJob();

	private:
		static void WINAPI FiberProc(void* param)
		{
			Fiber* self = static_cast<Fiber*>(param);

			while (true) 
			{
				if (self->m_job)
				{
					self->m_job->Execute();
					self->m_job = nullptr;
				}
				SwitchToFiber(self->m_mainFiber);
			}
		}

	private:
		Sptr<job::Job>	m_job;

		void*			m_fiber = nullptr;
		void*			m_mainFiber = nullptr;
	};
}
