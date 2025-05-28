#pragma once

namespace jam::utils::thread
{
    /*----------------
	    RW SpinLock
	-----------------*/

	/*--------------------------------------------
	[WWWWWWWW][WWWWWWWW][RRRRRRRR][RRRRRRRR]
	W : WriteFlag (Exclusive Lock Owner ThreadId)
	R : ReadFlag (Shared Lock Count)
	---------------------------------------------*/

    class Lock
    {
        enum : uint32
        {
            ACQUIRE_TIMEOUT_TICK = 10000,
            MAX_SPIN_COUNT = 5000,
            WRITE_THREAD_MASK = 0xFFFF'0000,
            READ_COUNT_MASK = 0x0000'FFFF,
            EMPTY_FLAG = 0x0000'0000
        };

    public:
        void            WriteLock(const char* name);
        void            WriteUnlock(const char* name);
        void            ReadLock(const char* name);
        void            ReadUnlock(const char* name);

    private:
        Atomic<uint32>  m_lockFlag = EMPTY_FLAG;
        uint16          m_writeCount = 0;
    };

    /*----------------
        LockGuards
    -----------------*/

    class ReadLockGuard
    {
    public:
        ReadLockGuard(Lock& lock, const char* name) : m_lock(lock), m_name(name) { m_lock.ReadLock(name); }
        ~ReadLockGuard() { m_lock.ReadUnlock(m_name); }

    private:
        Lock&           m_lock;
        const char*     m_name;
    };

    class WriteLockGuard
    {
    public:
        WriteLockGuard(Lock& lock, const char* name) : m_lock(lock), m_name(name) { m_lock.WriteLock(name); }
        ~WriteLockGuard() { m_lock.WriteUnlock(m_name); }

    private:
        Lock&           m_lock;
        const char*     m_name;
    };
}


#define USE_MANY_LOCKS(count)	jam::utils::thread::Lock _locks[count];
#define USE_LOCK				USE_MANY_LOCKS(1)
#define	READ_LOCK_IDX(idx)		jam::utils::thread::ReadLockGuard readLockGuard_##idx(_locks[idx], typeid(this).name());
#define READ_LOCK				READ_LOCK_IDX(0)
#define	WRITE_LOCK_IDX(idx)		jam::utils::thread::WriteLockGuard writeLockGuard_##idx(_locks[idx], typeid(this).name());
#define WRITE_LOCK				WRITE_LOCK_IDX(0)

