#include "pch.h"
#include "WorldContentsLoader.h"

#include <Cpp/M1/m1_world_contents.generated.hpp>

#include <cmath>
#include <stdexcept>

namespace m1
{
	namespace
	{
		using GeneratedSelector = jam::shared::gen::ePortalDtoDestinationSelector;

		jam::net::eWorldDestinationSelector ToRuntimeSelector(GeneratedSelector selector)
		{
			switch (selector)
			{
			case GeneratedSelector::DefaultForArchetype:
				return jam::net::eWorldDestinationSelector::DefaultForArchetype;
			case GeneratedSelector::ExplicitInstance:
				return jam::net::eWorldDestinationSelector::ExplicitInstance;
			case GeneratedSelector::AuthoredDestination:
				return jam::net::eWorldDestinationSelector::AuthoredDestination;
			}

			throw std::runtime_error("unsupported M1 portal destination selector");
		}

		jam::px::Transform BuildPose(const std::string& worldName, const std::string& spawnName, const jam::shared::gen::SpawnPoseDto& dto)
		{
			if (dto.p.size() != 3 || dto.q.size() != 4)
				throw std::runtime_error("invalid player spawn pose dimensions: " + worldName + "/" + spawnName);

			jam::px::Transform pose{
				.p = jam::px::Vec3(dto.p[0], dto.p[1], dto.p[2]),
				.q = jam::px::Quat(dto.q[0], dto.q[1], dto.q[2], dto.q[3]),
			};
			if (!pose.p.IsFinite() || !pose.q.IsFinite() || pose.q.MagnitudeSquared() <= jam::px::EPSILON * jam::px::EPSILON)
				throw std::runtime_error("non-finite or degenerate player spawn pose: " + worldName + "/" + spawnName);

			pose.q.Normalize();
			return pose;
		}

		PortalDefinition BuildPortal(const std::string& sourceWorldName, jam::net::WorldArchetypeKey sourceWorldKey, const jam::shared::gen::PortalDto& dto)
		{
			PortalDefinition definition{};
			definition.sourceWorldArchetypeKey		  = sourceWorldKey;
			definition.actorId						  = jam::net::ActorId(dto.actorId);
			definition.portal.destinationArchetypeKey = jam::net::MakeWorldArchetypeKey(dto.destinationWorldArchetype);
			definition.portal.selector				  = ToRuntimeSelector(dto.destinationSelector);
			definition.portal.explicitInstanceId	  = { dto.explicitInstanceId };
			definition.portal.destinationName		  = dto.destinationName;
			definition.portal.arrivalSpawn			  = dto.arrivalSpawn;

			if (!definition.actorId.IsValid())
				throw std::runtime_error("invalid portal actor id in world: " + sourceWorldName);
			
			if (!jam::IsValidAssetKey(definition.portal.destinationArchetypeKey))
				throw std::runtime_error("invalid portal destination world in world: " + sourceWorldName);
			
			if (definition.portal.arrivalSpawn.empty())
				throw std::runtime_error("portal arrival spawn must not be empty in world: " + sourceWorldName);
			
			if (definition.portal.selector == jam::net::eWorldDestinationSelector::ExplicitInstance
				&& !definition.portal.explicitInstanceId.IsValid())
			{
				throw std::runtime_error("explicit portal destination requires an instance id in world: " + sourceWorldName);
			}
			
			if (definition.portal.selector == jam::net::eWorldDestinationSelector::AuthoredDestination
				&& definition.portal.destinationName.empty())
			{
				throw std::runtime_error("authored portal destination requires a name in world: " + sourceWorldName);
			}

			return definition;
		}

		WorldContentsData BuildWorld(const std::string& worldName, const jam::shared::gen::WorldContentsDto& dto)
		{
			WorldContentsData data{};
			data.worldArchetypeName		  = worldName;
			data.worldArchetypeKey		  = jam::net::MakeWorldArchetypeKey(worldName);
			data.playerActorArchetypeName = dto.playerActorArchetype;
			data.playerActorArchetypeKey  = jam::net::MakeActorArchetypeKey(dto.playerActorArchetype);
			data.defaultPlayerSpawn		  = dto.defaultPlayerSpawn;

			if (data.worldArchetypeName.empty() || !jam::IsValidAssetKey(data.worldArchetypeKey))
				throw std::runtime_error("invalid M1 world archetype name: " + worldName);
			if (data.playerActorArchetypeName.empty() || !jam::IsValidAssetKey(data.playerActorArchetypeKey))
				throw std::runtime_error("invalid player actor archetype in world: " + worldName);
			if (data.defaultPlayerSpawn.empty())
				throw std::runtime_error("default player spawn must not be empty in world: " + worldName);

			data.playerSpawns.reserve(dto.playerSpawns.size());
			for (const auto& [spawnName, spawnDto] : dto.playerSpawns)
			{
				if (spawnName.empty())
					throw std::runtime_error("player spawn name must not be empty in world: " + worldName);

				PlayerSpawnData spawn{
					.name = spawnName,
					.pose = BuildPose(worldName, spawnName, spawnDto),
				};
				if (!data.playerSpawns.emplace(spawn.name, std::move(spawn)).second)
					throw std::runtime_error("duplicate player spawn in world: " + worldName + "/" + spawnName);
			}

			if (!data.FindDefaultPlayerSpawn())
				throw std::runtime_error("default player spawn was not defined in world: " + worldName);

			data.portals.reserve(dto.portals.size());
			for (const auto& portalDto : dto.portals)
				data.portals.emplace_back(BuildPortal(worldName, data.worldArchetypeKey, portalDto));

			return data;
		}

		void ValidatePortalReferences(const WorldContentsDatabase& database)
		{
			for (const auto& [sourceName, source] : database.worldsByName)
			{
				for (const PortalDefinition& portal : source.portals)
				{
					const WorldContentsData* target = database.Find(portal.portal.destinationArchetypeKey);
					if (!target)
					{
						throw std::runtime_error("portal target world contents were not defined: " + sourceName);
					}
					if (!target->FindPlayerSpawn(portal.portal.arrivalSpawn))
					{
						throw std::runtime_error("portal arrival spawn was not defined in target world: " + sourceName + "/" + portal.portal.arrivalSpawn);
					}
				}
			}
		}
	}

	jam::shared::gen::M1WorldContentsRootDto WorldContentsLoader::LoadDto(const std::string& path)
	{
		return jam::shared::gen::LoadM1WorldContentsRootDto(path);
	}

	WorldContentsDatabase WorldContentsLoader::Load(const std::string& path)
	{
		return WorldContentsDatabaseBuilder::Build(LoadDto(path));
	}

	WorldContentsDatabase WorldContentsDatabaseBuilder::Build(const jam::shared::gen::M1WorldContentsRootDto& dto)
	{
		if (dto.version != 1)
			throw std::runtime_error("unsupported M1 world contents version");

		WorldContentsDatabase database{};
		database.version = dto.version;
		database.worldsByName.reserve(dto.worlds.size());
		database.worldsByKey.reserve(dto.worlds.size());

		for (const auto& [worldName, worldDto] : dto.worlds)
		{
			WorldContentsData data = BuildWorld(worldName, worldDto);
			auto [it, inserted] = database.worldsByName.emplace(data.worldArchetypeName, std::move(data));
			if (!inserted)
				throw std::runtime_error("duplicate M1 world contents name: " + worldName);

			const WorldContentsData* stored = &it->second;
			if (!database.worldsByKey.emplace(stored->worldArchetypeKey, stored).second)
				throw std::runtime_error("duplicate M1 world contents key: " + worldName);
		}

		ValidatePortalReferences(database);
		return database;
	}
}
