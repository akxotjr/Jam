#pragma once

#include <jambase/JamTypes.h>
#include <jambase/JamAsset.h>

#include <jampx/PhysicsTypes.h>

#include <string>
#include <string_view>
#include <unordered_map>

namespace jam::net
{
	struct ActorArchetypeTag;
	using ActorArchetypeKey = AssetKey<ActorArchetypeTag>;

	inline ActorArchetypeKey MakeActorArchetypeKey(std::string_view name) noexcept
	{
		return MakeAssetKey<ActorArchetypeTag>(name);
	}

	struct ActorArchetypeData
	{
		ActorArchetypeKey		key{};
		std::string				name;
		px::PhysicsArchetypeKey	physicsArchetype{};
		std::string				physicsArchetypeName;
	};

	struct ActorArchetypeDatabase
	{
		int32 version = 1;
		std::unordered_map<ActorArchetypeKey, ActorArchetypeData> archetypes;

		const ActorArchetypeData* Find(ActorArchetypeKey key) const;
	};
}
