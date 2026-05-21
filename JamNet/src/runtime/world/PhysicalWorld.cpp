#include "pch.h"
#include "jamnet/runtime/world/PhysicalWorld.h"

namespace jam::net
{
	PhysicalWorld::PhysicalWorld(const WorldConfig& config)
		: WorldMembershipHost(config)
	{
	}

	void PhysicalWorld::SetRuntimeState(ePhysicalWorldRuntimeState runtime)
	{
		if (m_runtimeState == runtime)
			return;

		const ePhysicalWorldRuntimeState previous = m_runtimeState;
		m_runtimeState = runtime;

		if (runtime == ePhysicalWorldRuntimeState::Active)
		{
			if (previous == ePhysicalWorldRuntimeState::Paused)
			{
				Resume(m_tickIntervalNs);
				return;
			}

			if (previous != ePhysicalWorldRuntimeState::Active)
				Start(m_tickIntervalNs);
			return;
		}

		if (previous == ePhysicalWorldRuntimeState::Active)
			Stop();
	}
}
