#pragma once

#include "jamnet/runtime/world/lifecycle/WorldIdentity.h"

#include <string>
#include <string_view>
#include <unordered_map>

namespace jam::net
{
	enum class eWorldInstanceStartup : uint8
	{
		Bootstrap = 0,
		OnDemand = 1,
	};

	enum class eWorldInstanceLifecycle : uint8
	{
		Persistent = 0,
		DestroyWhenEmpty = 1,
	};

	struct WorldInstanceDefinition
	{
		WorldInstanceId			instanceId = kInvalidWorldInstanceId;
		std::string				name;
		WorldArchetypeKey		archetypeKey = {};
		std::string				archetypeName;
		eWorldInstanceStartup	startup = eWorldInstanceStartup::OnDemand;
		eWorldInstanceLifecycle	lifecycle = eWorldInstanceLifecycle::Persistent;
		bool					defaultForArchetype = false;

		bool IsValid() const noexcept
		{
			return instanceId.IsValid()
				&& !name.empty()
				&& IsValidAssetKey(archetypeKey)
				&& !archetypeName.empty();
		}

		WorldInstanceRef Ref() const noexcept
		{
			return { .instanceId = instanceId, .archetypeKey = archetypeKey };
		}
	};

	// An authored destination resolves to a logical instance before runtime
	// allocation. It intentionally carries no NetWorldId.
	struct WorldDestinationDefinition
	{
		std::string			name;
		WorldInstanceRef	instance = {};

		bool IsValid() const noexcept { return !name.empty() && instance.IsValid(); }
	};

	struct WorldInstanceDatabase
	{
		int32 version = 1;
		std::unordered_map<std::string, WorldInstanceDefinition> definitionsByName;
		std::unordered_map<WorldInstanceId, const WorldInstanceDefinition*, WorldInstanceIdHash> definitionsById;
		std::unordered_multimap<WorldArchetypeKey, const WorldInstanceDefinition*> definitionsByArchetype;

		const WorldInstanceDefinition* Find(std::string_view name) const;
		const WorldInstanceDefinition* Find(WorldInstanceId instanceId) const;
		const WorldInstanceDefinition* FindDefault(WorldArchetypeKey archetypeKey) const;
	};
}
