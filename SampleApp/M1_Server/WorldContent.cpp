#include "pch.h"
#include "WorldContent.h"

#include <jamnet/runtime/world/simulation/server/ServerWorld.h>

namespace m1
{
	WorldContent::WorldContent(
		const WorldContentsData& contents,
		std::shared_ptr<CharacterSessionStore> characterSessions)
		: m_contents(contents)
		, m_characterSessions(std::move(characterSessions))
		, m_portals(m_contents.portals)
	{
	}

	bool WorldContent::Initialize(jam::net::ServerWorld& world)
	{
		return m_portals.Initialize(world);
	}

	void WorldContent::OnPhysicsEvents(jam::net::ServerWorld& world, std::span<const jam::px::PhysicsEvent> events)
	{
		m_portals.OnPhysicsEvents(world, events);
	}

	void WorldContent::PrepareMemberContent(
		jam::net::ServerWorld& world,
		const jam::net::ServerWorldMemberContentContext& context,
		PrepareMemberCompletion completion)
	{
		const std::string_view spawnName = context.entryPoint.empty()
			? std::string_view(m_contents.defaultPlayerSpawn)
			: std::string_view(context.entryPoint);
		const PlayerSpawnData* spawn = m_contents.FindPlayerSpawn(spawnName);
		PersistentCharacterState character;
		if (!m_characterSessions || !spawn
			|| !m_characterSessions->BeginMaterialization(
				context.accountId, context.userId, context.transitionToken, context.correlation.world, character))
		{
			if (completion) completion(false);
			return;
		}

		jam::net::SpawnParams params{};
		params.clientRequestId = static_cast<jam::net::ClientRequestId>(context.transitionToken.value);
		if (params.clientRequestId == jam::net::kInvalidClientRequestId)
			params.clientRequestId = 1;
		params.actorArchetypeKey = character.actorArchetypeKey;
		params.owner = context.userId;
		params.controller = context.userId;
		params.desc.spawnSrc = jam::px::eSpawnSource::Runtime;
		params.desc.pose = spawn->pose;
		params.desc.overrides = jam::px::CharacterSpawnOverrides{};

		const jam::net::UserId userId = context.userId;
		const jam::net::WorldTransitionToken token = context.transitionToken;
		const jam::net::WorldEventCorrelation correlation = context.correlation;
		world.SpawnPlayerAsync(userId, correlation, params,
			[this, &world, userId, token, correlation, completion = std::move(completion)](
				jam::net::ActorId actorId, jam::net::ePlayerSpawnFailure failure) mutable
			{
				const PlayerActorBinding binding{
					.world = correlation.world,
					.actorId = actorId,
				};
				const bool succeeded = failure == jam::net::ePlayerSpawnFailure::None
					&& actorId.IsValid()
					&& m_characterSessions->CompleteMaterialization(userId, token, binding);
				if (!succeeded)
				{
					if (actorId.IsValid())
						world.DespawnActor(actorId, userId);
					m_characterSessions->FailMaterialization(userId, token);
				}
				if (completion) completion(succeeded);
			});
	}

	void WorldContent::RollbackMemberContent(
		jam::net::ServerWorld& world,
		jam::net::UserId userId,
		jam::net::WorldTransitionToken transitionToken)
	{
		if (!m_characterSessions)
			return;
		const auto target = m_characterSessions->RollbackMaterialization(
			userId, transitionToken, world.GetWorldRef());
		if (target && target->world == world.GetWorldRef())
			world.DespawnActor(target->actorId, userId);
	}

	bool WorldContent::CommitMemberLeave(
		jam::net::ServerWorld& world,
		jam::net::UserId userId,
		jam::net::WorldTransitionToken transitionToken)
	{
		if (!m_characterSessions)
			return false;
		const auto source = m_characterSessions->CommitLeave(
			userId, transitionToken, world.GetWorldRef());
		return !source || world.DespawnActor(source->actorId, userId);
	}

	bool WorldContent::RestoreMemberContent(
		jam::net::ServerWorld& world,
		const jam::net::WorldUserContext& user,
		jam::net::WorldTransitionToken)
	{
		if (!m_characterSessions)
			return false;
		const auto player = m_characterSessions->FindActivePlayer(user.userId, world.GetWorldRef());
		return player && world.RestorePlayerControl(user.userId, player->actorId);
	}
}
