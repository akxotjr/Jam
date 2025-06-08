#include "pch.h"
#include "Worker.h"
#include "Fiber.h"
#include "FiberScheduler.h"
#include "WorkerPool.h"
#include "TimeManager.h"

namespace jam::utils::thrd
{
	Worker::Worker()
	{
	}

	void Worker::Init()
	{
		// initialize about Fiber
		m_scheduler = make_unique<FiberScheduler>();
		m_mainFiber = ConvertThreadToFiber(nullptr);
	}

	void Worker::Run()
	{
		while (true)
		{
			DoBaseJob();

			DoJobs(); // JobQueue에서 Fiber 등록

			while (auto fiberOpt = m_scheduler->NextFiber())
			{
				fiberOpt.value()->SwitchTo(); // Fiber 실행
			}

			AdjustSleepInterval();
			std::this_thread::yield(); // or sleep_for
		}
	}

	void Worker::SetBaseJob(const job::Job& job)
	{
		m_baseJob = make_unique<job::Job>(job);
	}

	void Worker::DoBaseJob()
	{
		if (m_baseJob)
			m_baseJob->Execute();
	}

	void Worker::DoJobs()
	{
		const double now = TimeManager::Instance().GetCurrentTime();
		if (now >= tl_EndTime)
		{
			return;
		}

		if (m_currentJobQueue == nullptr)
			m_currentJobQueue = m_owner.lock()->m_globalQueue->Pop().get();

		while (true)
		{
			double nowInner = TimeManager::Instance().GetCurrentTime();
			if (nowInner >= tl_EndTime)
				break;

			m_currentJobQueue->ExecuteFront();
		}

		m_currentJobQueue = nullptr;

		if (m_steal)
			Steal();
	}

	void Worker::AdjustSleepInterval()
	{
		int32 workCount = m_workCount;
		m_workCount.store(0);

		double delay = 0.0;
		if (workCount == 0)
			delay = 0.01;
		else if (workCount < 5)
			delay = 0.005;
		else
			delay = 0.001;

		tl_EndTime = TimeManager::Instance().GetCurrentTime() + delay;
	}

	void Worker::Steal()
	{
		const double now = TimeManager::Instance().GetCurrentTime();

		if (now >= tl_EndTime)
			return;

		job::JobQueue* jobQueue = m_owner.lock()->GetJobQueueFromAnotherWorker();
		jobQueue->ExecuteBack();
	}
}
