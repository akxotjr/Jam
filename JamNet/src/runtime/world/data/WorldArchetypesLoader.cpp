#include "pch.h"

#include "jambase/JsonFileIO.h"
#include "jamnet/runtime/world/data/WorldArchetypesLoader.h"

#include <Cpp/world_archetypes.generated.hpp>

#include <nlohmann/json.hpp>
#include <stdexcept>

namespace jam::net
{
	namespace
	{
		void ValidateWorldArchetypeIdentity(const std::string& name, WorldArchetypeKey key)
		{
			if (name.empty())
				throw std::runtime_error("world archetype name must not be empty");

			const auto expectedKey = MakeWorldArchetypeKey(name);
			if (!IsValidAssetKey(expectedKey))
				throw std::runtime_error("world archetype key must not be zero: " + name);
			if (expectedKey != key)
				throw std::runtime_error("world archetype key mismatch: " + name);
		}

		void ValidateWorldTemplateIdentity(const std::string& name, WorldTemplateKey key)
		{
			if (name.empty())
				throw std::runtime_error("world template name must not be empty");

			const WorldTemplateKey expectedKey = MakeWorldTemplateKey(name);
			if (!IsValidAssetKey(key))
				throw std::runtime_error("world template key must not be zero: " + name);
			if (expectedKey != key)
				throw std::runtime_error("world template key mismatch: " + name);
		}

		WorldArchetypeData BuildData(const std::string& name, const jam::shared::gen::WorldArchetypeDto& dto)
		{
			WorldArchetypeData data{};
			data.name = name;
			data.archetypeKey = WorldArchetypeKey::FromU64(dto.archetypeKey);
			data.templateName = dto.templateName;
			data.templateKey = WorldTemplateKey::FromU64(dto.templateKey);
			data.presentationName = dto.presentationName;
			data.actorArchetypesName = dto.actorArchetypesName;
			data.actorLevelName = dto.actorLevelName;
			data.physicsAssetName = dto.physicsAssetName;

			ValidateWorldArchetypeIdentity(data.name, data.archetypeKey);
			ValidateWorldTemplateIdentity(data.templateName, data.templateKey);
			if (data.presentationName.empty())
				throw std::runtime_error("world archetype entry requires non-empty presentation_name: " + name);

			return data;
		}
	}

	WorldArchetypesLoader::json WorldArchetypesLoader::LoadJson(const std::string& path)
	{
		return JsonFileIO::Load(path, "failed to open world archetype file for read: ",
			[](const json&)
			{
			});
	}

	jam::shared::gen::WorldArchetypesRootDto WorldArchetypesLoader::LoadDto(const std::string& path)
	{
		return jam::shared::gen::LoadWorldArchetypesRootDto(path);
	}

	jam::shared::gen::WorldArchetypesRootDto WorldArchetypesLoader::LoadDtoFromJson(const json& json)
	{
		return jam::shared::gen::DeserializeWorldArchetypesRootDto(json);
	}

	WorldArchetypeDatabase WorldArchetypesLoader::Load(const std::string& path)
	{
		return WorldArchetypeDatabaseBuilder::Build(LoadDto(path));
	}

	WorldArchetypeDatabase WorldArchetypeDatabaseBuilder::Build(const jam::shared::gen::WorldArchetypesRootDto& dto)
	{
		WorldArchetypeDatabase database{};
		database.version = dto.version;
		if (database.version != 1)
			throw std::runtime_error("unsupported world archetype asset version");

		database.archetypesByName.reserve(dto.worldArchetypes.size());
		database.archetypesByKey.reserve(dto.worldArchetypes.size());

		for (const auto& [name, archetypeDto] : dto.worldArchetypes)
		{
			WorldArchetypeData data = BuildData(name, archetypeDto);
			auto [it, inserted] = database.archetypesByName.emplace(data.name, std::move(data));
			if (!inserted)
				throw std::runtime_error("duplicate world archetype name: " + name);

			const WorldArchetypeData* stored = &it->second;
			if (!database.archetypesByKey.emplace(stored->archetypeKey, stored).second)
				throw std::runtime_error("duplicate world archetype key: " + stored->name);
		}

		return database;
	}
}
