#pragma once

#include "PortalTypes.h"

#include <jamnet/runtime/world/actor/ActorArchetypeDatabase.h>
#include <jamnet/runtime/world/data/WorldArchetypeDatabase.h>

#include <jampx/PhysicsTypes.h>

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace m1
{
	struct PlayerSpawnData
	{
		std::string			name;
		jam::px::Transform	pose;
	};

	struct WorldContentsData
	{
		std::string											worldArchetypeName;
		jam::net::WorldArchetypeKey							worldArchetypeKey{};
		std::string											playerActorArchetypeName;
		jam::net::ActorArchetypeKey							playerActorArchetypeKey{};
		std::string											defaultPlayerSpawn;
		std::unordered_map<std::string, PlayerSpawnData>	playerSpawns;
		std::vector<PortalDefinition>						portals;

		const PlayerSpawnData* FindPlayerSpawn(std::string_view name) const;
		const PlayerSpawnData* FindDefaultPlayerSpawn() const;
	};

	struct WorldContentsDatabase
	{
		int32 version = 1;
		std::unordered_map<std::string, WorldContentsData> worldsByName;
		std::unordered_map<jam::net::WorldArchetypeKey, const WorldContentsData*> worldsByKey;

		const WorldContentsData* Find(std::string_view name) const;
		const WorldContentsData* Find(jam::net::WorldArchetypeKey key) const;
	};
}
