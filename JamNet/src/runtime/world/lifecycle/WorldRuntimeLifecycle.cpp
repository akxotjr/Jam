#include "pch.h"
#include "jamnet/runtime/world/lifecycle/WorldRuntimeLifecycle.h"

namespace jam::net
{
	eWorldRuntimeLifecycleDecision WorldRuntimeLifecycle::Evaluate(const WorldRuntimeLifecycleInput& input)
	{
		if (!input.runtime || input.policy == eWorldInstanceLifecycle::Persistent)
			return eWorldRuntimeLifecycleDecision::Keep;
		if (input.runtime->memberCount != 0 || input.runtime->pendingAttachCount != 0 || input.runtime->lifecyclePinCount != 0)
			return eWorldRuntimeLifecycleDecision::Keep;
		if (input.emptySinceNs == 0 || input.nowNs < input.emptySinceNs + input.emptyTtlNs)
			return eWorldRuntimeLifecycleDecision::Keep;
		return input.runtime->state == eWorldRuntimeState::Draining
			? eWorldRuntimeLifecycleDecision::Destroy
			: eWorldRuntimeLifecycleDecision::BeginDrain;
	}
}
