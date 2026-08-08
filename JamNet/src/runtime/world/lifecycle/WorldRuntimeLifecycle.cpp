#include "pch.h"
#include "jamnet/runtime/world/lifecycle/WorldRuntimeLifecycle.h"

namespace jam::net
{
	eWorldRuntimeLifecycleDecision WorldRuntimeLifecycle::Evaluate(const WorldRuntimeLifecycleInput& input)
	{
		if (!input.world || input.policy == eWorldInstanceLifecycle::Persistent)
			return eWorldRuntimeLifecycleDecision::Keep;
		if (input.world->memberCount != 0 || input.world->pendingAttachCount != 0 || input.world->lifecyclePinCount != 0)
			return eWorldRuntimeLifecycleDecision::Keep;
		if (input.emptySinceNs == 0 || input.nowNs < input.emptySinceNs + input.emptyTtlNs)
			return eWorldRuntimeLifecycleDecision::Keep;
		return input.world->state == eWorldRuntimeState::Draining
			? eWorldRuntimeLifecycleDecision::Destroy
			: eWorldRuntimeLifecycleDecision::BeginDrain;
	}
}
