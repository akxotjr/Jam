#include "pch.h"
#include "WorldContent.h"
#include "../M1_Shared/BotScenarioPlacement.h"

#include <jamnet/runtime/world/simulation/server/ServerWorld.h>
#include <jamnet/runtime/world/simulation/common/ActorComponents.h>

namespace m1
{
	WorldContent::WorldContent(
		const WorldContentsData& contents,
		const WorldInstanceContentsData& instance,
		std::shared_ptr<CharacterSessionStore> characterSessions)
		: m_contents(contents)
		, m_characterSessions(std::move(characterSessions))
		, m_portals(instance.portals)
	{
	}

	void WorldContent::OnPhysicsEvents(jam::net::ServerWorld& world, std::span<const jam::px::PhysicsEvent> events)
	{
		m_portals.OnPhysicsEvents(world, events);
	}

}

namespace m1
{
	bool WorldContent::OnJoiningMembership(jam::net::ServerWorld& world, const jam::net::WorldMembershipContext& context)
	{
		const auto character = m_characterSessions ? m_characterSessions->FindSelectedCharacter(context.userId) : std::nullopt;
		const std::string_view spawnName = context.entryPoint.empty() ? std::string_view(m_contents.defaultPlayerSpawn) : std::string_view(context.entryPoint);
		return character && character->accountId == context.accountId && m_contents.FindPlayerSpawn(spawnName)
			&& (!context.entryPoint.empty() || character->worldArchetypeKey == world.GetWorldInstance().archetypeKey);
	}

	bool WorldContent::OnPreparedMembership(jam::net::ServerWorld& world, const jam::net::WorldMembershipContext& context)
	{
		const auto character = m_characterSessions ? m_characterSessions->FindSelectedCharacter(context.userId) : std::nullopt;
		if (character) m_entryCharacters.emplace(context.transitionToken, *character);
		const std::string_view spawnName = context.entryPoint.empty() ? std::string_view(m_contents.defaultPlayerSpawn) : std::string_view(context.entryPoint);
		PersistentCharacterState selected;
		const PlayerSpawnData* spawn = m_contents.FindPlayerSpawn(spawnName);
		bool succeeded = character && spawn
			&& (context.entryPoint.empty() ? character->worldArchetypeKey == world.GetWorldInstance().archetypeKey : true)
			&& m_characterSessions->BeginMembershipMaterialization(context.accountId, context.userId, context.transitionToken, context.correlation.world, selected);
		if (succeeded)
		{
			jam::px::Vec3 position = context.entryPoint.empty() ? selected.position : spawn->pose.p;
			if (m1::shared::IsBotAccount(context.accountId) && !m_contents.traverseLanes.empty())
			{
				const auto placement = m1::shared::MakeBotTraversePlacement(context.accountId, static_cast<uint32>(m_contents.traverseLanes.size()));
				const auto& lane = m_contents.traverseLanes[placement.laneIndex];
				position = lane.start + lane.direction * (lane.length * placement.phase);
			}
			// Retain the intended location even if the client disconnects before Player creation.
			// Target rollback restores m_entryCharacters when server admission fails.
			succeeded = m_characterSessions->UpdateLocation(context.userId, world.GetWorldInstance().archetypeKey, position);
		}
		return succeeded;
	}

	bool WorldContent::OnActivatedMembership(jam::net::ServerWorld& world, const jam::net::WorldMembershipContext& context)
	{
		const std::string_view spawnName = context.entryPoint.empty() ? std::string_view(m_contents.defaultPlayerSpawn) : std::string_view(context.entryPoint);
		const PlayerSpawnData* spawn = m_contents.FindPlayerSpawn(spawnName);

		const auto selected = m_characterSessions ? m_characterSessions->FindSelectedCharacter(context.userId) : std::nullopt;
		if (!spawn || !selected)
		{
			return false;
		}

		const PersistentCharacterState character = *selected;
		if (const auto previous = m_entryPlayers.find(context.transitionToken); previous != m_entryPlayers.end())
		{
			bool removed = world.ResolveActor(previous->second.actorId) == entt::null;
			if (!removed) world.DespawnActorAsync(context.userId, previous->second.actorId, [&removed](bool result) { removed = result; });
			if (!removed) return false;
			m_entryPlayers.erase(previous);
			if (!m_characterSessions->ResetMembershipActivation(context.userId, context.transitionToken, context.correlation.world))
			{
				return false;
			}
		}

		jam::net::SpawnParams params{};

		params.clientRequestId = static_cast<jam::net::ClientRequestId>(context.transitionToken.value);
		if (params.clientRequestId == jam::net::kInvalidClientRequestId)
			params.clientRequestId = 1;

		params.actorArchetypeKey = character.actorArchetypeKey;
		params.desc.spawnSrc = jam::px::eSpawnSource::Runtime;
		params.desc.pose = spawn->pose;
		if (m1::shared::IsBotAccount(context.accountId) && !m_contents.traverseLanes.empty())
		{
			const auto placement = m1::shared::MakeBotTraversePlacement(
				context.accountId, static_cast<uint32>(m_contents.traverseLanes.size()));
			const TraverseLaneData& lane = m_contents.traverseLanes[placement.laneIndex];
			params.desc.pose.p = lane.start + lane.direction * (lane.length * placement.phase);
		}
		if (context.entryPoint.empty())
		{
			if (character.worldArchetypeKey != context.correlation.world.instance.archetypeKey)
			{

				return false;
			}
			if (!m1::shared::IsBotAccount(context.accountId) || m_contents.traverseLanes.empty())
				params.desc.pose.p = character.position;
		}
		params.desc.overrides = jam::px::CharacterSpawnOverrides{};

		const jam::net::UserId userId = context.userId;
		const jam::net::WorldTransitionToken token = context.transitionToken;
		const jam::net::WorldEventCorrelation correlation = context.correlation;

		jam::net::ActorId actorId = jam::net::ActorId::Invalid();
		jam::net::ePlayerSpawnFailure failure = jam::net::ePlayerSpawnFailure::SpawnFailed;
		// This engine operation completes immediately on the World shard.
		world.SpawnPlayerAsync(userId, correlation, params,
			[&actorId, &failure](jam::net::ActorId result, jam::net::ePlayerSpawnFailure reason)
			{
				actorId = result;
				failure = reason;
			});
		const PlayerActorBinding binding{ .world = correlation.world, .actorId = actorId, };
		const bool succeeded = failure == jam::net::ePlayerSpawnFailure::None && actorId.IsValid()
			&& m_characterSessions->CompleteMembershipMaterialization(userId, token, binding)
			&& m_characterSessions->UpdateLocation(userId, correlation.world.instance.archetypeKey, params.desc.pose.p);
		if (actorId.IsValid()) m_entryPlayers[token] = binding;
		if (succeeded)
		{
			m_entryPlayers.erase(token);
			m_characterSessions->AcceptMembershipMaterialization(userId, token);
		}
		else if (actorId.IsValid())
		{
			// Spawn has returned; failed activation can now be cleaned up directly.
			bool removed = world.ResolveActor(actorId) == entt::null;
			if (!removed) world.DespawnActorAsync(userId, actorId, [&removed](bool result) { removed = result; });
			if (removed && m_characterSessions->ResetMembershipActivation(userId, token, correlation.world))
				m_entryPlayers.erase(token);
		}
		return succeeded;
	}

}

namespace m1
{
	bool WorldContent::OnInitialize(jam::net::ServerWorld& world)
	{
		return m_portals.Initialize(world);
	}

	void WorldContent::OnClose(jam::net::ServerWorld&)
	{
		m_entryPlayers.clear();
		m_entryCharacters.clear();
		m_detachedMembers.clear();
	}

	bool WorldContent::OnDetachingMembership(jam::net::ServerWorld& world, jam::net::UserId userId, jam::net::WorldTransitionToken token)
	{
		if (!m_characterSessions || m_detachedMembers.contains(token)) return false;
		auto player = m_characterSessions->FindActivePlayer(userId, world.GetWorldRef());
		if (!player)
		{
			const auto entity = world.GetPlayerEntity(userId);
			if (entity != entt::null)
				player = PlayerActorBinding{ world.GetWorldRef(), world.GetRegistry().get<jam::net::ActorId>(entity) };
		}
		const auto character = m_characterSessions->FindSelectedCharacter(userId);
		if (!character) return false;
		PersistentCharacterState saved = *character;
		auto& registry = world.GetRegistry();
		const auto entity = player ? world.ResolveActor(player->actorId) : entt::null;
		if (player && (entity == entt::null || !registry.valid(entity))) return false;
		if (const auto* state = entity != entt::null ? registry.try_get<jam::net::CharAuthorityState>(entity) : nullptr)
			saved.position = state->state.pos;
		saved.worldArchetypeKey = world.GetWorldInstance().archetypeKey;
		if (!m_characterSessions->UpdateLocation(userId, saved.worldArchetypeKey, saved.position)) return false;
		m_detachedMembers.emplace(token, DetachedMember{ userId, player.value_or(PlayerActorBinding{ .world = world.GetWorldRef() }), std::move(saved) });
		return true;
	}

	bool WorldContent::OnRollbackMembership(jam::net::ServerWorld& world, jam::net::UserId userId, jam::net::WorldTransitionToken token)
	{
		if (!m_characterSessions) return true;
		const auto player = m_entryPlayers.find(token);
		if (player != m_entryPlayers.end())
		{
			if (world.ResolveActor(player->second.actorId) != entt::null)
			{
				bool removed = false;
				// This engine operation completes immediately on the World shard.
				world.DespawnActorAsync(userId, player->second.actorId, [&removed](bool result) { removed = result; });
				if (!removed) return false;
			}
			m_entryPlayers.erase(player);
		}
		if (!m_characterSessions->CancelMembershipMaterialization(userId, token, world.GetWorldRef())) return false;
		if (const auto saved = m_entryCharacters.find(token); saved != m_entryCharacters.end())
		{
			if (!m_characterSessions->UpdateLocation(userId, saved->second.worldArchetypeKey, saved->second.position)) return false;
			m_entryCharacters.erase(saved);
		}
		return true;
	}

	bool WorldContent::OnRemovedMembership(jam::net::ServerWorld& world, jam::net::UserId userId, jam::net::WorldTransitionToken token)
	{
		const auto source = m_detachedMembers.find(token);
		if (!m_characterSessions || source == m_detachedMembers.end() || source->second.userId != userId) return false;
		// Leave the saved binding intact when despawn fails, allowing a retry.
		if (world.ResolveActor(source->second.player.actorId) != entt::null)
		{
			bool removed = false;
			world.DespawnActorAsync(userId, source->second.player.actorId, [&removed](bool result) { removed = result; });
			if (!removed) return false;
		}
		if (!m_characterSessions->FinalizeMembershipSource(userId, token, source->second.player)) return false;
		std::erase_if(m_entryPlayers, [&](const auto& entry) { return entry.second.actorId == source->second.player.actorId; });
		m_detachedMembers.erase(source);
		return true;
	}

	bool WorldContent::OnRestoredMembership(jam::net::ServerWorld& world, const jam::net::WorldUserContext& user, jam::net::WorldTransitionToken token)
	{
		const auto source = m_detachedMembers.find(token);
		if (!m_characterSessions || source == m_detachedMembers.end() || source->second.userId != user.userId) return false;
		if (source->second.player.actorId.IsValid() && !world.RestorePlayerBinding(user.userId, source->second.player.actorId)) return false;
		if (!m_characterSessions->UpdateLocation(user.userId, source->second.character.worldArchetypeKey, source->second.character.position)) return false;
		m_detachedMembers.erase(source);
		return true;
	}

	void WorldContent::OnCommittedMembership(jam::net::ServerWorld&, jam::net::UserId userId, jam::net::WorldTransitionToken token)
	{
		m_entryPlayers.erase(token);
		m_entryCharacters.erase(token);
		if (m_characterSessions) m_characterSessions->AcceptMembershipMaterialization(userId, token);
	}
}
