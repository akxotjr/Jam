#pragma once

#include <jamnet/runtime/world/actor/ActorId.h>
#include <jamnet/runtime/world/lifecycle/WorldTransitionTypes.h>

namespace m1
{
	struct PortalComponent
	{
		jam::net::WorldArchetypeKey			destinationArchetypeKey = {};
		jam::net::eWorldDestinationSelector	selector				= jam::net::eWorldDestinationSelector::DefaultForArchetype;
		jam::net::WorldInstanceId			explicitInstanceId		= jam::net::kInvalidWorldInstanceId;
		std::string							destinationName;
		std::string							arrivalSpawn;
	};

	struct PortalDefinition
	{
		jam::net::WorldArchetypeKey			sourceWorldArchetypeKey{};
		jam::net::ActorId					actorId = jam::net::ActorId::Invalid();
		PortalComponent						portal;
	};
}
