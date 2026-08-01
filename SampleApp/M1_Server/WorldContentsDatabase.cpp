#include "pch.h"
#include "WorldContentsDatabase.h"

namespace m1
{
	const PlayerSpawnData* WorldContentsData::FindPlayerSpawn(std::string_view name) const
	{
		const auto it = playerSpawns.find(std::string(name));
		return it != playerSpawns.end() ? &it->second : nullptr;
	}

	const PlayerSpawnData* WorldContentsData::FindDefaultPlayerSpawn() const
	{
		return FindPlayerSpawn(defaultPlayerSpawn);
	}

	const WorldContentsData* WorldContentsDatabase::Find(std::string_view name) const
	{
		const auto it = worldsByName.find(std::string(name));
		return it != worldsByName.end() ? &it->second : nullptr;
	}

	const WorldContentsData* WorldContentsDatabase::Find(jam::net::WorldArchetypeKey key) const
	{
		const auto it = worldsByKey.find(key);
		return it != worldsByKey.end() ? it->second : nullptr;
	}
}
