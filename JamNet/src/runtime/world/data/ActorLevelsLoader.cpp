#include "pch.h"
#include "jamnet/runtime/world/data/ActorLevelsLoader.h"

#include "jambase/JsonFileIO.h"

#include <Cpp/actor_levels.generated.hpp>

#include <nlohmann/json.hpp>
#include <stdexcept>

namespace jam::net
{
	namespace
	{
		void ValidateActorArchetypeIdentity(const std::string& name, ActorArchetypeKey key)
		{
			if (name.empty())
				throw std::runtime_error("actor level actor_archetype must not be empty");

			const auto expectedKey = MakeActorArchetypeKey(name);
			if (!key)
				throw std::runtime_error("actor level actor_archetype_key must not be zero: " + name);
			if (expectedKey != key)
				throw std::runtime_error("actor level actor_archetype key mismatch: " + name);
		}

		float RequireComponent(const std::vector<float>& values, size_t index, const char* fieldName, size_t expectedCount)
		{
			if (values.size() != expectedCount)
				throw std::runtime_error(std::string(fieldName) + " must contain " + std::to_string(expectedCount) + " components");
			return values.at(index);
		}

		px::Transform BuildTransform(const jam::shared::gen::SpawnPoseDto& dto)
		{
			return px::Transform
			{
				.p = px::Vec3(
					RequireComponent(dto.p, 0, "actor level pose.p", 3),
					RequireComponent(dto.p, 1, "actor level pose.p", 3),
					RequireComponent(dto.p, 2, "actor level pose.p", 3)),
				.q = px::Quat(
					RequireComponent(dto.q, 0, "actor level pose.q", 4),
					RequireComponent(dto.q, 1, "actor level pose.q", 4),
					RequireComponent(dto.q, 2, "actor level pose.q", 4),
					RequireComponent(dto.q, 3, "actor level pose.q", 4)),
			};
		}

		ActorLevelInstanceData BuildInstance(const jam::shared::gen::ActorLevelInstanceDto& dto)
		{
			ActorLevelInstanceData data{};
			data.levelActorId = dto.levelActorId;
			data.actorArchetypeName = dto.actorArchetype;
			if (dto.actorArchetypeKey != 0)
				data.actorArchetype = ActorArchetypeKey::FromU64(dto.actorArchetypeKey);
			else if (!data.actorArchetypeName.empty())
				data.actorArchetype = MakeActorArchetypeKey(data.actorArchetypeName);
			data.pose = BuildTransform(dto.spawnPose);

			ValidateActorArchetypeIdentity(data.actorArchetypeName, data.actorArchetype);
			return data;
		}
	}

	ActorLevelsLoader::json ActorLevelsLoader::LoadJson(const std::string& path)
	{
		return JsonFileIO::Load(path, "failed to open actor level file for read: ",
			[](const json&)
			{
			});
	}

	jam::shared::gen::ActorLevelsRootDto ActorLevelsLoader::LoadDto(const std::string& path)
	{
		return jam::shared::gen::LoadActorLevelsRootDto(path);
	}

	jam::shared::gen::ActorLevelsRootDto ActorLevelsLoader::LoadDtoFromJson(const json& json)
	{
		return jam::shared::gen::DeserializeActorLevelsRootDto(json);
	}

	ActorLevelDatabase ActorLevelsLoader::Load(const std::string& path)
	{
		return ActorLevelDatabaseBuilder::Build(LoadDto(path));
	}

	ActorLevelDatabase ActorLevelDatabaseBuilder::Build(const jam::shared::gen::ActorLevelsRootDto& dto)
	{
		ActorLevelDatabase database{};
		database.version = dto.version;
		if (database.version != 1)
			throw std::runtime_error("unsupported actor level asset version");

		database.sceneName = dto.sceneName;
		database.instances.reserve(dto.instances.size());
		for (const auto& instanceDto : dto.instances)
			database.instances.push_back(BuildInstance(instanceDto));

		return database;
	}
}
