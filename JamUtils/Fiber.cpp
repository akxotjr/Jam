#include "pch.h"
#include "Fiber.h"

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
		SwitchToFiber(m_fiber);
	}
}
