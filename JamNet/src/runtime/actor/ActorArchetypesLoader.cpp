#include "pch.h"

#include "jambase/JsonFileIO.h"
#include "jamnet/runtime/actor/ActorArchetypesLoader.h"

#include <Cpp/actor_archetypes.generated.hpp>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace jam::net
{
	namespace
	{
		void ValidateArchetypeIdentity(const std::string& name, ActorArchetypeKey key)
		{
			if (name.empty())
				throw std::runtime_error("actor archetype name must not be empty");

			const auto expectedKey = MakeActorArchetypeKey(name);
			if (!key)
				throw std::runtime_error("actor archetype key must not be zero: " + name);
			if (expectedKey != key)
				throw std::runtime_error("actor archetype key mismatch: " + name);
		}

		void ValidatePhysicsArchetypeIdentity(const std::string& name, px::PhysicsArchetypeKey key)
		{
			if (name.empty())
				throw std::runtime_error("physics archetype name must not be empty");

			const auto expectedKey = px::MakePhysicsArchetypeKey(name);
			if (!IsValidAssetKey(key))
				throw std::runtime_error("physics archetype key must not be zero: " + name);
			if (expectedKey != key)
				throw std::runtime_error("physics archetype key mismatch: " + name);
		}

		ActorArchetypeData BuildData(const std::string& name, const jam::shared::gen::ActorArchetypeDto& dto)
		{
			ActorArchetypeData data{};
			data.name = name;
			data.key = ActorArchetypeKey::FromU64(dto.archetypeKey);
			data.physicsArchetypeName = dto.physicsArchetype;
			if (dto.physicsArchetypeKey != 0)
				data.physicsArchetype = px::PhysicsArchetypeKey{ dto.physicsArchetypeKey };
			else if (!data.physicsArchetypeName.empty())
				data.physicsArchetype = px::MakePhysicsArchetypeKey(data.physicsArchetypeName);

			ValidateArchetypeIdentity(data.name, data.key);
			if (!data.physicsArchetypeName.empty())
				ValidatePhysicsArchetypeIdentity(data.physicsArchetypeName, data.physicsArchetype);

			return data;
		}
	}

	ActorArchetypesLoader::json ActorArchetypesLoader::LoadJson(const std::string& path)
	{
		return JsonFileIO::Load(path, "failed to open actor archetype file for read: ",
			[](const json&)
			{
			});
	}

	jam::shared::gen::ActorArchetypesRootDto ActorArchetypesLoader::LoadDto(const std::string& path)
	{
		return jam::shared::gen::LoadActorArchetypesRootDto(path);
	}

	jam::shared::gen::ActorArchetypesRootDto ActorArchetypesLoader::LoadDtoFromJson(const json& json)
	{
		return jam::shared::gen::DeserializeActorArchetypesRootDto(json);
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
