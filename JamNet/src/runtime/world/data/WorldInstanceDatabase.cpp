#include "pch.h"
#include "jamnet/runtime/world/data/WorldInstanceDatabase.h"

namespace jam::net
{
	const WorldInstanceDefinition* WorldInstanceDatabase::Find(std::string_view name) const
	{
		auto it = definitionsByName.find(std::string(name));
		return (it != definitionsByName.end()) ? &it->second : nullptr;
	}

	const WorldInstanceDefinition* WorldInstanceDatabase::Find(WorldInstanceId instanceId) const
	{
		if (!instanceId.IsValid())
			return nullptr;

		auto it = definitionsById.find(instanceId);
		return (it != definitionsById.end()) ? it->second : nullptr;
	}

	const WorldInstanceDefinition* WorldInstanceDatabase::FindDefault(WorldArchetypeKey archetypeKey) const
	{
		if (!IsValidAssetKey(archetypeKey))
			return nullptr;

		const auto [first, last] = definitionsByArchetype.equal_range(archetypeKey);
		for (auto it = first; it != last; ++it)
		{
			if (it->second->defaultForArchetype)
				return it->second;
		}
		return nullptr;
	}
}
