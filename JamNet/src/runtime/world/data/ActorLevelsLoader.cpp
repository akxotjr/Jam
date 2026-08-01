#include "pch.h"
#include "jamnet/runtime/world/data/ActorLevelsLoader.h"
#include "jamnet/runtime/world/actor/ActorDirectory.h"

#include <Cpp/actor_levels.generated.hpp>

#include <stdexcept>
#include <unordered_set>

namespace jam::net
{
	namespace
	{
		void ValidateActorArchetypeIdentity(const std::string& name)
		{
			if (name.empty())
				throw std::runtime_error("actor level actor_archetype must not be empty");

			const auto expectedKey = MakeActorArchetypeKey(name);
			if (!expectedKey)
				throw std::runtime_error("derived actor level actor_archetype key must not be zero: " + name);
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
			data.actorId = dto.actorId;
			data.actorArchetypeName = dto.actorArchetype;
			if (!data.actorArchetypeName.empty())
				data.actorArchetype = MakeActorArchetypeKey(data.actorArchetypeName);
			data.pose = BuildTransform(dto.spawnPose);

			ValidateActorArchetypeIdentity(data.actorArchetypeName);
			return data;
		}
	}

	jam::shared::gen::ActorLevelsRootDto ActorLevelsLoader::LoadDto(const std::string& path)
	{
		return jam::shared::gen::LoadActorLevelsRootDto(path);
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
		std::unordered_set<uint32> actorIds;
		for (const auto& instanceDto : dto.instances)
		{
			ActorLevelInstanceData instance = BuildInstance(instanceDto);
			if (!ActorDirectory::IsInitialId(ActorId(instance.actorId)))
				throw std::runtime_error("actor level actor_id must be a canonical generation-1 ActorId");

			if (!actorIds.insert(instance.actorId).second)
				throw std::runtime_error("actor level contains duplicate actor_id: " + std::to_string(instance.actorId));

			database.instances.push_back(std::move(instance));
		}

		return database;
	}
}
