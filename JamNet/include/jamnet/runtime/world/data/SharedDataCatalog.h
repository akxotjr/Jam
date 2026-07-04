#pragma once

#include <jambase/JamTypes.h>
#include <jambase/JamAsset.h>

#include <string>
#include <string_view>
#include <unordered_map>

namespace jam::net
{
	struct SharedDataCatalogKeyTag;
	using SharedDataCatalogKey = AssetKey<SharedDataCatalogKeyTag>;

	inline SharedDataCatalogKey MakeSharedDataCatalogKey(std::string_view name) noexcept
	{
		return MakeAssetKey<SharedDataCatalogKeyTag>(name);
	}

	struct SharedDataCatalogEntry
	{
		SharedDataCatalogKey key{};
		std::string          name;
		std::string          path;

		bool IsValid() const noexcept
		{
			return IsValidAssetKey(key) && !name.empty() && !path.empty();
		}
	};

	struct SharedDataCatalog
	{
		int32 version = 1;

		std::unordered_map<std::string, SharedDataCatalogEntry> actorArchetypeSetsByName;
		std::unordered_map<SharedDataCatalogKey, const SharedDataCatalogEntry*> actorArchetypeSetsByKey;

		std::unordered_map<std::string, SharedDataCatalogEntry> physicsAssetsByName;
		std::unordered_map<SharedDataCatalogKey, const SharedDataCatalogEntry*> physicsAssetsByKey;

		std::unordered_map<std::string, SharedDataCatalogEntry> actorLevelsByName;
		std::unordered_map<SharedDataCatalogKey, const SharedDataCatalogEntry*> actorLevelsByKey;

		const SharedDataCatalogEntry* FindActorArchetypeSet(std::string_view name) const;
		const SharedDataCatalogEntry* FindActorArchetypeSet(SharedDataCatalogKey key) const;

		const SharedDataCatalogEntry* FindPhysicsAsset(std::string_view name) const;
		const SharedDataCatalogEntry* FindPhysicsAsset(SharedDataCatalogKey key) const;

		const SharedDataCatalogEntry* FindActorLevel(std::string_view name) const;
		const SharedDataCatalogEntry* FindActorLevel(SharedDataCatalogKey key) const;
	};
}
