#include "pch.h"

#if JAMNET_USE_MIMALLOC
#include <mimalloc-new-delete.h>
#endif

#include "jamnet/core/memory/DefaultAllocator.h"

namespace jam
{
	void InitializeDefaultAllocator()
	{
#if JAMNET_USE_MIMALLOC
		(void)mi_version();
#endif
	}

	const char* DefaultAllocatorName() noexcept
	{
#if JAMNET_USE_MIMALLOC
		return "mimalloc";
#else
		return "system";
#endif
	}
}
