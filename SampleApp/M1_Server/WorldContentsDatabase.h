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

	struct TraverseLaneData
	{
		jam::px::Vec3 start = jam::px::Vec3::Zero();
		jam::px::Vec3 direction = jam::px::Vec3::Zero();
		float length = 0.0f;
	};

	struct HotspotData
	{
		jam::px::Vec3 center = jam::px::Vec3::Zero();
		float halfExtentX = 0.0f;
		float halfExtentZ = 0.0f;
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
		std::vector<TraverseLaneData>						traverseLanes;
		std::vector<HotspotData>							hotspots;

		const PlayerSpawnData* FindPlayerSpawn(std::string_view name) const;
		const PlayerSpawnData* FindDefaultPlayerSpawn() const;
	};

	struct WorldInstanceContentsData
	{
		std::string						name;
		jam::net::WorldInstanceId		instanceId = jam::net::kInvalidWorldInstanceId;
		jam::net::WorldArchetypeKey		worldArchetypeKey{};
		std::vector<PortalDefinition>	portals;
	};

	struct WorldContentsDatabase
	{
		int32 version = 1;
		std::unordered_map<std::string, WorldContentsData>						  worldsByName;
		std::unordered_map<jam::net::WorldArchetypeKey, const WorldContentsData*> worldsByKey;
		std::unordered_map<std::string, WorldInstanceContentsData>				  instancesByName;
		std::unordered_map<jam::net::WorldInstanceId, const WorldInstanceContentsData*, jam::net::WorldInstanceIdHash> instancesById;

		const WorldContentsData*		 Find(std::string_view name) const;
		const WorldContentsData*		 Find(jam::net::WorldArchetypeKey key) const;
		const WorldInstanceContentsData* Find(jam::net::WorldInstanceId instanceId) const;
	};
}
