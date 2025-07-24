#include "pch.h"
#include "Fiber.h"
#include "FiberScheduler.h"

namespace jam::utils::thrd
{
	Fiber::Fiber()
	{
		m_fiber = CreateFiber(0, FiberProc, this);
	}

	Fiber::~Fiber()
	{
		if (m_fiber)
			DeleteFiber(m_fiber);
	}

	void Fiber::BindJob(Sptr<job::Job> job, void* mainFiber)
	{
		m_job = job;	// todo
		m_mainFiber = mainFiber;
	}

	void Fiber::SwitchTo()
	{
		tl_Worker->GetScheduler()->SetCurrentFiber(this);
		::SwitchToFiber(m_fiber);
	}
}
