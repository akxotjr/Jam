#include "pch.h"

#include "jamnet/runtime/world/data/WorldArchetypeDatabase.h"

namespace jam::net
{
	const WorldArchetypeData* WorldArchetypeDatabase::Find(std::string_view name) const
	{
		if (const auto it = archetypesByName.find(std::string(name)); it != archetypesByName.end())
			return &it->second;
		return nullptr;
	}

	const WorldArchetypeData* WorldArchetypeDatabase::Find(WorldArchetypeKey key) const
	{
		if (const auto it = archetypesByKey.find(key); it != archetypesByKey.end())
			return it->second;
		return nullptr;
	}
}
