#include "pch.h"
#include "Worker.h"
#include "Fiber.h"
#include "FiberScheduler.h"
#include "WorkerPool.h"

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
		const uint64 now = ::GetTickCount64();
		if (now >= tl_EndTime)
		{
			return;
		}

		if (m_currentJobQueue == nullptr)
			m_currentJobQueue = m_owner.lock()->m_globalQueue->Pop().get();

		while (true)
		{
			uint64 nowInner = ::GetTickCount64();
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

		uint64 delay = 0;
		if (workCount == 0)
			delay = 10;
		else if (workCount < 5)
			delay = 5;
		else
			delay = 1;

		tl_EndTime = ::GetTickCount64(); + delay;
	}

	void Worker::Steal()
	{
		const uint64 now = ::GetTickCount64();

		if (now >= tl_EndTime)
			return;

		job::JobQueue* jobQueue = m_owner.lock()->GetJobQueueFromAnotherWorker();
		jobQueue->ExecuteBack();
	}
}
