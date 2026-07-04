#include "pch.h"

#include "jamnet/runtime/world/data/SharedDataCatalog.h"

namespace jam::net
{
	const SharedDataCatalogEntry* SharedDataCatalog::FindActorArchetypeSet(std::string_view name) const
	{
		if (const auto it = actorArchetypeSetsByName.find(std::string(name)); it != actorArchetypeSetsByName.end())
			return &it->second;
		return nullptr;
	}

	const SharedDataCatalogEntry* SharedDataCatalog::FindActorArchetypeSet(SharedDataCatalogKey key) const
	{
		if (const auto it = actorArchetypeSetsByKey.find(key); it != actorArchetypeSetsByKey.end())
			return it->second;
		return nullptr;
	}

	const SharedDataCatalogEntry* SharedDataCatalog::FindPhysicsAsset(std::string_view name) const
	{
		if (const auto it = physicsAssetsByName.find(std::string(name)); it != physicsAssetsByName.end())
			return &it->second;
		return nullptr;
	}

	const SharedDataCatalogEntry* SharedDataCatalog::FindPhysicsAsset(SharedDataCatalogKey key) const
	{
		if (const auto it = physicsAssetsByKey.find(key); it != physicsAssetsByKey.end())
			return it->second;
		return nullptr;
	}

	const SharedDataCatalogEntry* SharedDataCatalog::FindActorLevel(std::string_view name) const
	{
		if (const auto it = actorLevelsByName.find(std::string(name)); it != actorLevelsByName.end())
			return &it->second;
		return nullptr;
	}

	const SharedDataCatalogEntry* SharedDataCatalog::FindActorLevel(SharedDataCatalogKey key) const
	{
		if (const auto it = actorLevelsByKey.find(key); it != actorLevelsByKey.end())
			return it->second;
		return nullptr;
	}
}
