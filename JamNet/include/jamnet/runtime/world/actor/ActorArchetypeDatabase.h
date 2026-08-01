#pragma once

#include <jambase/JamTypes.h>
#include <jambase/JamAsset.h>

#include <jampx/PhysicsTypes.h>

#include <string>
#include <string_view>
#include <unordered_map>

namespace jam::net
{
	enum class eActorSpawnPolicy : uint8
	{
		LevelOnly,
		RuntimeOnly,
		Both,
	};

	enum class eActorSpawnSource : uint8
	{
		Level,
		Runtime,
	};

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
		eActorSpawnPolicy		spawnPolicy = eActorSpawnPolicy::Both;
		bool					allowReplication = true;

		bool AllowsSpawn(eActorSpawnSource source) const noexcept
		{
			return spawnPolicy == eActorSpawnPolicy::Both
				|| (source == eActorSpawnSource::Level && spawnPolicy == eActorSpawnPolicy::LevelOnly)
				|| (source == eActorSpawnSource::Runtime && spawnPolicy == eActorSpawnPolicy::RuntimeOnly);
		}
	};

	struct ActorArchetypeDatabase
	{
		int32 version = 1;
		std::unordered_map<ActorArchetypeKey, ActorArchetypeData> archetypes;

		const ActorArchetypeData* Find(ActorArchetypeKey key) const;
	};
}
