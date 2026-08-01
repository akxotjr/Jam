#include "pch.h"

#include "jamnet/runtime/world/actor/ActorArchetypesLoader.h"

#include <Cpp/actor_archetypes.generated.hpp>
#include <stdexcept>

namespace jam::net
{
	namespace
	{
		eActorSpawnPolicy ToSpawnPolicy(jam::shared::gen::eActorArchetypeDtoSpawnPolicy policy)
		{
			switch (policy)
			{
			case jam::shared::gen::eActorArchetypeDtoSpawnPolicy::LevelOnly: return eActorSpawnPolicy::LevelOnly;
			case jam::shared::gen::eActorArchetypeDtoSpawnPolicy::RuntimeOnly: return eActorSpawnPolicy::RuntimeOnly;
			case jam::shared::gen::eActorArchetypeDtoSpawnPolicy::Both: return eActorSpawnPolicy::Both;
			}
			throw std::runtime_error("unsupported actor spawn policy");
		}

		void ValidateArchetypeIdentity(const std::string& name)
		{
			if (name.empty())
				throw std::runtime_error("actor archetype name must not be empty");

			const auto expectedKey = MakeActorArchetypeKey(name);
			if (!expectedKey)
				throw std::runtime_error("derived actor archetype key must not be zero: " + name);
		}

		void ValidatePhysicsArchetypeIdentity(const std::string& name)
		{
			if (name.empty())
				throw std::runtime_error("physics archetype name must not be empty");

			const auto expectedKey = px::MakePhysicsArchetypeKey(name);
			if (!IsValidAssetKey(expectedKey))
				throw std::runtime_error("derived physics archetype key must not be zero: " + name);
		}

		ActorArchetypeData BuildData(const std::string& name, const jam::shared::gen::ActorArchetypeDto& dto)
		{
			ActorArchetypeData data{};
			data.name = name;
			data.key = MakeActorArchetypeKey(name);
			data.physicsArchetypeName = dto.physicsArchetype;
			data.spawnPolicy = ToSpawnPolicy(dto.spawnPolicy);
			data.allowReplication = dto.allowReplication;
			if (!data.physicsArchetypeName.empty())
				data.physicsArchetype = px::MakePhysicsArchetypeKey(data.physicsArchetypeName);

			ValidateArchetypeIdentity(data.name);
			if (!data.physicsArchetypeName.empty())
				ValidatePhysicsArchetypeIdentity(data.physicsArchetypeName);

			return data;
		}
	}

	jam::shared::gen::ActorArchetypesRootDto ActorArchetypesLoader::LoadDto(const std::string& path)
	{
		return jam::shared::gen::LoadActorArchetypesRootDto(path);
	}

	ActorArchetypeDatabase ActorArchetypesLoader::Load(const std::string& path)
	{
		return ActorArchetypeDatabaseBuilder::Build(LoadDto(path));
	}

	ActorArchetypeDatabase ActorArchetypeDatabaseBuilder::Build(const jam::shared::gen::ActorArchetypesRootDto& dto)
	{
		ActorArchetypeDatabase database{};
		database.version = dto.version;
		if (database.version != 1)
			throw std::runtime_error("unsupported actor archetype asset version");

		database.archetypes.reserve(dto.archetypes.size());
		for (const auto& [name, archetypeDto] : dto.archetypes)
		{
			ActorArchetypeData data = BuildData(name, archetypeDto);
			if (!database.archetypes.emplace(data.key, std::move(data)).second)
				throw std::runtime_error("duplicate actor archetype key: " + name);
		}

		return database;
	}
}
