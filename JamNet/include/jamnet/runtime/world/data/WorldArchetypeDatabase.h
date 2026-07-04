#pragma once

#include <jambase/JamTypes.h>
#include <jambase/JamAsset.h>

#include "jamnet/runtime/world/types/WorldTemplateKey.h"

#include <string>
#include <string_view>
#include <unordered_map>

namespace jam::net
{
	struct WorldArchetypeTag;
	using WorldArchetypeKey = AssetKey<WorldArchetypeTag>;

	inline WorldArchetypeKey MakeWorldArchetypeKey(std::string_view name) noexcept
	{
		return MakeAssetKey<WorldArchetypeTag>(name);
	}

	struct WorldArchetypeData
	{
		WorldArchetypeKey	archetypeKey = {};
		std::string			name;

		WorldTemplateKey	templateKey = {};
		std::string			templateName;

		std::string			presentationName;
		std::string			actorArchetypesName;
		std::string			actorLevelName;
		std::string			physicsAssetName;

		bool IsValid() const noexcept
		{
			return IsValidAssetKey(archetypeKey) && !name.empty() && IsValidAssetKey(templateKey) && !templateName.empty();
		}
	};

	struct WorldArchetypeDatabase
	{
		int32 version = 1;
		std::unordered_map<std::string, WorldArchetypeData>				archetypesByName;
		std::unordered_map<WorldArchetypeKey, const WorldArchetypeData*> archetypesByKey;

		const WorldArchetypeData* Find(std::string_view name) const;
		const WorldArchetypeData* Find(WorldArchetypeKey key) const;
	};
}
