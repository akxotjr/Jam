#include "pch.h"
#include "jamnet/core/memory/MemoryManager.h"
#include "jamnet/core/memory/MemoryPool.h"

namespace jam
{

	void MemoryManager::Init()
	{
		int32 size = 0;
		int32 tableIndex = 0;

		for (size = 32; size <= 1024; size += 32)
		{
			MemoryPool* pool = new MemoryPool(size);
			m_pools.push_back(pool);

			while (tableIndex <= size)
			{
				m_poolTable[tableIndex] = pool;
				tableIndex++;
			}
		}

		for (; size <= 2048; size += 128)
		{
			MemoryPool* pool = new MemoryPool(size);
			m_pools.push_back(pool);

			while (tableIndex <= size)
			{
				m_poolTable[tableIndex] = pool;
				tableIndex++;
			}
		}

		for (; size <= 4096; size += 256)
		{
			MemoryPool* pool = new MemoryPool(size);
			m_pools.push_back(pool);

			while (tableIndex <= size)
			{
				m_poolTable[tableIndex] = pool;
				tableIndex++;
			}
		}
	}


	void MemoryManager::Shutdown()
	{
		for (MemoryPool* pool : m_pools)
			delete pool;

		m_pools.clear();
	}


	void* MemoryManager::Allocate(int32 size)
	{
		MemoryHeader* header = nullptr;
		const int32 allocSize = size + sizeof(MemoryHeader);

		if (allocSize > MAX_ALLOC_SIZE)
		{
			header = reinterpret_cast<MemoryHeader*>(::_aligned_malloc(allocSize, SLIST_ALIGNMENT));
		}
		else
		{
			header = m_poolTable[allocSize]->Pop();
		}

		return MemoryHeader::AttachHeader(header, allocSize);
	}

	void MemoryManager::Release(void* ptr)
	{
		MemoryHeader* header = MemoryHeader::DetachHeader(ptr);
		const int32 allocSize = header->allocSize;
		JAMNET_ASSERT(allocSize > 0);

		if (allocSize > MAX_ALLOC_SIZE)
		{
			::_aligned_free(header);
		}
		else
		{
			m_poolTable[allocSize]->Push(header);
		}
	}

}
