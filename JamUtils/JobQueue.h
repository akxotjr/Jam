#pragma once
#include "Job.h"
#include "JobTimer.h"
#include "LockQueue.h"
#include "ObjectPool.h"


namespace jam::utils::job
{
	USING_SHARED_PTR(JobQueue)

	class JobQueue : public enable_shared_from_this<JobQueue>
	{
	public:
		void DoAsync(CallbackType&& callback)
		{
			Push(ObjectPool<Job>::MakeShared(std::move(callback)));
		}

		template<typename T, typename Ret, typename... Args>
		void DoAsync(Ret(T::* memFunc)(Args...), Args... args)
		{
			shared_ptr<T> owner = static_pointer_cast<T>(shared_from_this());
			Push(memory::ObjectPool<Job>::MakeShared(owner, memFunc, std::forward<Args>(args)...));
		}

		void DoTimer(double afterTime, CallbackType&& callback)
		{
			JobRef job = memory::ObjectPool<Job>::MakeShared(std::move(callback));
			JobTimer::Instance()->Reserve(afterTime, shared_from_this(), job);
		}

		template<typename T, typename Ret, typename... Args>
		void DoTimer(double afterTime, Ret(T::* memFunc)(Args...), Args... args)
		{
			shared_ptr<T> owner = static_pointer_cast<T>(shared_from_this());
			JobRef job = memory::ObjectPool<Job>::MakeShared(owner, memFunc, std::forward<Args>(args)...);
			JobTimer::Instance()->Reserve(afterTime, shared_from_this(), job);
		}

		void							ClearJobs() { _jobs.Clear(); }

		void							Push(JobRef job, bool pushOnly = false);
		void							Execute();

	protected:
		jam::utils::thread::LockQueue<JobRef>		_jobs;
		Atomic<int32>								_jobCount = 0;
	};
}

