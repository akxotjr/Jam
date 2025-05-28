#include "pch.h"
#include "MemoryPool.h"


namespace jam::utils::memory
{
	MemoryPool::MemoryPool(int32 allocSize) : m_allocSize(allocSize)
	{
		::InitializeSListHead(&m_header);
	}

	MemoryPool::~MemoryPool()
	{
		while (MemoryHeader* memory = static_cast<MemoryHeader*>(::InterlockedPopEntrySList(&m_header)))
			::_aligned_free(memory);
	}

	void MemoryPool::Push(MemoryHeader* ptr)
	{
		ptr->allocSize = 0;

		::InterlockedPushEntrySList(&m_header, static_cast<PSLIST_ENTRY>(ptr));

		m_useCount.fetch_sub(1);
		m_reserveCount.fetch_add(1);
	}

	MemoryHeader* MemoryPool::Pop()
	{
		MemoryHeader* memory = static_cast<MemoryHeader*>(::InterlockedPopEntrySList(&m_header));

		if (memory == nullptr)
		{
			memory = reinterpret_cast<MemoryHeader*>(::_aligned_malloc(m_allocSize, SLIST_ALIGNMENT));
		}
		else
		{
			ASSERT_CRASH(memory->allocSize == 0);
			m_reserveCount.fetch_sub(1);
		}

		m_useCount.fetch_add(1);

		return memory;
	}
}
