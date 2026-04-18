#pragma once

#ifndef JAMNET_USE_MIMALLOC
#define JAMNET_USE_MIMALLOC 1
#endif

#if JAMNET_USE_MIMALLOC
#include <mimalloc.h>
#else
#include <malloc.h>
#endif

namespace jam
{
	void		InitializeDefaultAllocator();
	const char* DefaultAllocatorName() noexcept;

	inline void* AllocAligned(size_t size, size_t alignment)
	{
#if JAMNET_USE_MIMALLOC
		return mi_malloc_aligned(size, alignment);
#else
		return _aligned_malloc(size, alignment);
#endif
	}

	inline void FreeAligned(void* ptr) noexcept
	{
#if JAMNET_USE_MIMALLOC
		mi_free(ptr);
#else
		_aligned_free(ptr);
#endif
	}
}

#define DEFAULT_ALLOCATOR_INIT()	::jam::InitializeDefaultAllocator()
#define DEFAULT_ALLOCATOR_NAME()	::jam::DefaultAllocatorName()