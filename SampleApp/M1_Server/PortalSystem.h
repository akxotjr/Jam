#pragma once

#include "PortalTypes.h"

#include <jampx/PhysicsTypes.h>

#include <span>
#include <vector>

namespace jam::net
{
	class ServerWorld;
}

namespace m1
{
	class PortalSystem
	{
	public:
		explicit PortalSystem(std::vector<PortalDefinition> definitions);

		bool Initialize(jam::net::ServerWorld& world);
		void OnPhysicsEvents(jam::net::ServerWorld& world, std::span<const jam::px::PhysicsEvent> events) const;

	private:
		bool TryEnterPortal(jam::net::ServerWorld& world, jam::px::ActorId triggerActorId, jam::px::ActorId otherActorId) const;

	private:
		std::vector<PortalDefinition> m_definitions;
	};
}
