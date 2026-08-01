#pragma once

#include "jamnet/runtime/world/lifecycle/WorldShardState.h"
#include "jamnet/runtime/world/data/WorldInstanceDatabase.h"

namespace jam::net
{
	enum class eWorldRuntimeLifecycleDecision : uint8
	{
		Keep		= 0,
		BeginDrain	= 1,
		Destroy		= 2,
	};

	struct WorldRuntimeLifecycleInput
	{
		const WorldRecord*		runtime			= nullptr;
		eWorldInstanceLifecycle policy			= eWorldInstanceLifecycle::Persistent;
		uint64					nowNs			= 0;
		uint64					emptySinceNs	= 0;
		uint64					emptyTtlNs		= 0;
	};

	class WorldRuntimeLifecycle
	{
	public:
		static eWorldRuntimeLifecycleDecision Evaluate(const WorldRuntimeLifecycleInput& input);
	};
}
