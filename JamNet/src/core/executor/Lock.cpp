#include "pch.h"
#include "jamnet/core/executor/Lock.h"
#include "jamnet/core/executor/DeadLockProfiler.h"
#include "jamnet/core/executor/ThreadContext.h"

namespace jam
{
	void Lock::WriteLock(const char* name)
	{
#if _DEBUG
		DeadLockProfiler::Instance().PushLock(name);
#endif

		const uint32 lockThreadId = (m_lockFlag.load() & WRITE_THREAD_MASK) >> 16;
		const uint32 threadId = CurrentThreadId();
		if (threadId == lockThreadId)
		{
			m_writeCount++;
			return;
		}

		const uint64 beginTick = ::GetTickCount64();
		const uint32 desired   = ((threadId << 16) & WRITE_THREAD_MASK);

		while (true)
		{
			for (uint32 spinCount = 0; spinCount < MAX_SPIN_COUNT; spinCount++)
			{
				uint32 expected = EMPTY_FLAG;
				if (m_lockFlag.compare_exchange_strong(OUT expected, desired))
				{
					m_writeCount++;
					return;
				}
			}

			if (::GetTickCount64() - beginTick >= ACQUIRE_TIMEOUT_TICK)
				JAM_CRASH("LOCK_TIMEOUT");

			std::this_thread::yield();
		}
	}

	void Lock::WriteUnlock(const char* name)
	{
#if _DEBUG
		DeadLockProfiler::Instance().PopLock(name);
#endif

		// ReadLock 다 풀기 전에는 WriteUnlock 불가능.
		if ((m_lockFlag.load() & READ_COUNT_MASK) != 0)
			JAM_CRASH("INVALID_UNLOCK_ORDER");

		const int32 lockCount = --m_writeCount;
		if (lockCount == 0)
			m_lockFlag.store(EMPTY_FLAG);
	}

	void Lock::ReadLock(const char* name)
	{
#if _DEBUG
		DeadLockProfiler::Instance().PushLock(name);
#endif

		const uint32 lockThreadId = (m_lockFlag.load() & WRITE_THREAD_MASK) >> 16;
		if (CurrentThreadId() == lockThreadId)
		{
			m_lockFlag.fetch_add(1);
			return;
		}

		const uint64 beginTick = ::GetTickCount64();
		while (true)
		{
			for (uint32 spinCount = 0; spinCount < MAX_SPIN_COUNT; spinCount++)
			{
				uint32 expected = (m_lockFlag.load() & READ_COUNT_MASK);
				if (m_lockFlag.compare_exchange_strong(OUT expected, expected + 1))
					return;
			}

			if (::GetTickCount64() - beginTick >= ACQUIRE_TIMEOUT_TICK)
				JAM_CRASH("LOCK_TIMEOUT");

			std::this_thread::yield();
		}
	}

	void Lock::ReadUnlock(const char* name)
	{
#if _DEBUG
		DeadLockProfiler::Instance().PopLock(name);
#endif

		if ((m_lockFlag.fetch_sub(1) & READ_COUNT_MASK) == 0)
			JAM_CRASH("MULTIPLE_UNLOCK");
	}
}
