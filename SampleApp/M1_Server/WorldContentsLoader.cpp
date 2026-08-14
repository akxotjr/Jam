#include "pch.h"
#include "WorldContentsLoader.h"

#include <SharedData/Cpp/world_contents.generated.hpp>

#include <cmath>
#include <stdexcept>

namespace m1
{
	namespace
	{
		jam::px::Vec3 BuildVec3(const std::vector<float>& values, const std::string& context)
		{
			if (values.size() != 3)
				throw std::runtime_error("invalid vec3 dimensions: " + context);
			
			jam::px::Vec3 result(values[0], values[1], values[2]);
			if (!result.IsFinite())
				throw std::runtime_error("non-finite vec3: " + context);
			
			return result;
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

		PortalDefinition BuildPortal(const std::string& worldName, jam::net::WorldArchetypeKey worldKey, const jam::shared::gen::PortalDto& dto)
		{
			PortalDefinition definition{};
			definition.name						= dto.name;
			definition.sourceWorldArchetypeKey	= worldKey;
			definition.actorId					= jam::net::ActorId(dto.actorId);
			definition.approachPosition			= BuildVec3(dto.approachPosition, worldName + "/" + dto.name);
			definition.portal.arrivalSpawn		= dto.arrivalSpawn;
			
			if (definition.name.empty() || !definition.actorId.IsValid() || definition.portal.arrivalSpawn.empty())
				throw std::runtime_error("invalid portal definition in world: " + worldName);
			
			return definition;
		}

		WorldContentsData BuildWorld(const std::string& worldName, const jam::shared::gen::WorldContentsDto& dto)
		{
			WorldContentsData data{};
			data.worldArchetypeName			= worldName;
			data.worldArchetypeKey			= jam::net::MakeWorldArchetypeKey(worldName);
			data.playerActorArchetypeName	= dto.playerActorArchetype;
			data.playerActorArchetypeKey	= jam::net::MakeActorArchetypeKey(dto.playerActorArchetype);
			data.defaultPlayerSpawn			= dto.defaultPlayerSpawn;

			if (worldName.empty() 
				|| !jam::IsValidAssetKey(data.worldArchetypeKey)
				|| data.playerActorArchetypeName.empty() 
				|| !jam::IsValidAssetKey(data.playerActorArchetypeKey)
				|| data.defaultPlayerSpawn.empty())
			{
				throw std::runtime_error("invalid M1 world contents: " + worldName);
			}

			for (const auto& [spawnName, spawnDto] : dto.playerSpawns)
			{
				PlayerSpawnData spawn{ .name = spawnName, .pose = BuildPose(worldName, spawnName, spawnDto) };
				if (spawnName.empty() || !data.playerSpawns.emplace(spawnName, std::move(spawn)).second)
					throw std::runtime_error("invalid or duplicate player spawn: " + worldName + "/" + spawnName);
			}

			if (!data.FindDefaultPlayerSpawn())
				throw std::runtime_error("default player spawn was not defined in world: " + worldName);

			for (const auto& portalDto : dto.portals)
				data.portals.emplace_back(BuildPortal(worldName, data.worldArchetypeKey, portalDto));

			for (size_t i = 0; i < dto.scenario.traverseLanes.size(); ++i)
			{
				const auto& laneDto = dto.scenario.traverseLanes[i];

				if (laneDto.direction.size() != 2 || laneDto.length <= 0.0f)
					throw std::runtime_error("invalid traverse lane in world: " + worldName);

				TraverseLaneData lane{
					.start		= BuildVec3(laneDto.start, worldName + "/lane" + std::to_string(i)),
					.direction	= jam::px::Vec3(laneDto.direction[0], 0.0f, laneDto.direction[1]),
					.length		= laneDto.length,
				};

				if (!lane.direction.IsFinite() || lane.direction.MagnitudeSquared() <= jam::px::EPSILON * jam::px::EPSILON)
					throw std::runtime_error("invalid traverse lane direction in world: " + worldName);
				
				data.traverseLanes.push_back(lane);
			}

			for (size_t i = 0; i < dto.scenario.hotspots.size(); ++i)
			{
				const auto& hotspotDto = dto.scenario.hotspots[i];
				if (hotspotDto.halfExtents.size() != 2 || hotspotDto.halfExtents[0] <= 0.0f || hotspotDto.halfExtents[1] <= 0.0f)
					throw std::runtime_error("invalid hotspot in world: " + worldName);

				data.hotspots.push_back({
					.center		 = BuildVec3(hotspotDto.center, worldName + "/hotspot" + std::to_string(i)),
					.halfExtentX = hotspotDto.halfExtents[0],
					.halfExtentZ = hotspotDto.halfExtents[1],
				});
			}

			return data;
		}

		WorldInstanceContentsData BuildInstance(const std::string& instanceName, const jam::shared::gen::InstanceContentDto& dto, const WorldContentsDatabase& database)
		{
			WorldInstanceContentsData instance{};
			instance.name				= instanceName;
			instance.instanceId			= jam::net::MakeStaticWorldInstanceId(instanceName);
			instance.worldArchetypeKey	= jam::net::MakeWorldArchetypeKey(dto.worldArchetype);
			
			const WorldContentsData* world = database.Find(instance.worldArchetypeKey);
			if (instanceName.empty() || !world)
				throw std::runtime_error("invalid instance world contents: " + instanceName);

			instance.portals.reserve(world->portals.size());
			for (const PortalDefinition& authoredPortal : world->portals)
			{
				const auto routeIt = dto.portalDestinations.find(authoredPortal.name);
				if (routeIt == dto.portalDestinations.end())
					throw std::runtime_error("missing portal route: " + instanceName + "/" + authoredPortal.name);

				const auto& route = routeIt->second;
				
				PortalDefinition portal = authoredPortal;
				
				portal.portal.destinationArchetypeKey	= jam::net::MakeWorldArchetypeKey(route.worldArchetype);
				portal.portal.selector					= jam::net::eWorldDestinationSelector::AuthoredDestination;
				portal.portal.destinationName			= route.destinationName;
				
				if (!database.Find(portal.portal.destinationArchetypeKey))
					throw std::runtime_error("missing portal target archetype: " + instanceName + "/" + authoredPortal.name);
			
				instance.portals.push_back(std::move(portal));
			}

			return instance;
		}
	}

	jam::shared::gen::WorldContentsRootDto WorldContentsLoader::LoadDto(const std::string& path)
	{
		return jam::shared::gen::LoadWorldContentsRootDto(path);
	}

	WorldContentsDatabase WorldContentsLoader::Load(const std::string& path)
	{
		return WorldContentsDatabaseBuilder::Build(LoadDto(path));
	}

	WorldContentsDatabase WorldContentsDatabaseBuilder::Build(const jam::shared::gen::WorldContentsRootDto& dto)
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
			
			auto [it, inserted] = database.worldsByName.emplace(worldName, std::move(data));
		
			if (!inserted || !database.worldsByKey.emplace(it->second.worldArchetypeKey, &it->second).second)
				throw std::runtime_error("duplicate M1 world contents: " + worldName);
		}

		database.instancesByName.reserve(dto.instances.size());
		database.instancesById.reserve(dto.instances.size());

		for (const auto& [instanceName, instanceDto] : dto.instances)
		{
			WorldInstanceContentsData instance = BuildInstance(instanceName, instanceDto, database);
			auto [it, inserted] = database.instancesByName.emplace(instanceName, std::move(instance));
			
			if (!inserted || !database.instancesById.emplace(it->second.instanceId, &it->second).second)
				throw std::runtime_error("duplicate M1 instance contents: " + instanceName);
		}

		for (const auto& [instanceName, instance] : database.instancesByName)
		{
			for (const PortalDefinition& portal : instance.portals)
			{
				const auto targetIt = database.instancesByName.find(portal.portal.destinationName);
				if (targetIt == database.instancesByName.end() || targetIt->second.worldArchetypeKey != portal.portal.destinationArchetypeKey)
				{
					throw std::runtime_error("invalid portal destination: " + instanceName + "/" + portal.name);
				}

				const WorldContentsData* targetWorld = database.Find(portal.portal.destinationArchetypeKey);
				
				if (!targetWorld || !targetWorld->FindPlayerSpawn(portal.portal.arrivalSpawn))
					throw std::runtime_error("invalid portal arrival spawn: " + instanceName + "/" + portal.name);
			}
		}

		return database;
	}
}
