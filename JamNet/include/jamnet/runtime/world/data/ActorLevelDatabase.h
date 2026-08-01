#pragma once

#include <jambase/JamTypes.h>

#include "jamnet/runtime/world/actor/ActorArchetypeDatabase.h"

#include <jampx/PhysicsTypes.h>

#include <string>
#include <vector>

namespace jam::net
{
	struct ActorLevelInstanceData
	{
		uint32				actorId = 0;
		ActorArchetypeKey	actorArchetype{};
		std::string			actorArchetypeName;
		px::Transform		pose{};
	};

	struct ActorLevelDatabase
	{
		int32								version = 1;
		std::string							sceneName;
		std::vector<ActorLevelInstanceData> instances;

		const ActorLevelInstanceData* Find(uint32 actorId) const;
	};
}
