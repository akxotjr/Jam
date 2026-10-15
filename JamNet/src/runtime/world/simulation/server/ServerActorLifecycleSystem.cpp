#include "pch.h"
#include "jamnet/runtime/world/simulation/server/ServerActorLifecycleSystem.h"
#include "jamnet/runtime/world/simulation/server/ServerWorld.h"
#include "jamnet/runtime/world/simulation/common/ActorComponents.h"

#include <algorithm>


namespace jam::net
{
	ServerActorLifecycleSystem::ServerActorLifecycleSystem(
		ServerWorld& world,
		entt::registry& registry,
		ActorDirectory& directory,
		const ActorArchetypeDatabase& archetypes)
		: m_world(world), m_registry(registry), m_directory(directory), m_archetypes(archetypes)
	{
	}

	ActorLifecycleResult ServerActorLifecycleSystem::PrepareSpawn(SpawnParams& params, bool clientRequest)
	{
		if (!IsValidAssetKey(params.actorArchetypeKey))
			return { .result = eActorLifecycleResult::InvalidRequest, .action = eActorAction::Spawn };

		const ActorArchetypeData* archetype = m_archetypes.Find(params.actorArchetypeKey);

		if (!archetype || !archetype->AllowsSpawn(eActorSpawnSource::Runtime) || !IsValidAssetKey(archetype->physicsArchetype))
			return { .result = eActorLifecycleResult::InvalidRequest, .action = eActorAction::Spawn };

		if (IsValidAssetKey(params.desc.archetype) && params.desc.archetype != archetype->physicsArchetype)
			return { .result = eActorLifecycleResult::InvalidRequest, .action = eActorAction::Spawn };

		params.desc.archetype = archetype->physicsArchetype;

		const px::ActorId bindingTarget = ResolveBindingTarget(params.targetActorId);
		if (params.targetActorId.IsValid() && bindingTarget == px::INVALID_ACTOR_ID)
			return { .result = eActorLifecycleResult::Failed, .action = eActorAction::Spawn };

		const auto result = PrepareActor(params, *archetype, clientRequest);
		if (!result.Succeeded()) return result;
		const entt::entity e = result.entity;
		params.desc.targetActorId = bindingTarget;
		if (params.targetActorId.IsValid())
		{
			m_registry.emplace<TargetInfo>(e, TargetInfo{
				.targetActorId = params.targetActorId,
				.resolvedActorId = bindingTarget,
			});
		}


		JAM_ASSERT(m_directory.Validate());
		return result;
	}

	ActorLifecycleResult ServerActorLifecycleSystem::PrepareLevelSpawn(const ActorLevelInstanceData& instance, SpawnParams& params)
	{
		const ActorArchetypeData* archetype = m_archetypes.Find(instance.actorArchetype);
		if (!archetype || !archetype->AllowsSpawn(eActorSpawnSource::Level)
			|| !IsValidAssetKey(archetype->key) || !IsValidAssetKey(archetype->physicsArchetype))
			return { .result = eActorLifecycleResult::InvalidRequest, .action = eActorAction::Spawn };

		const px::eBodyType bodyType = m_world.FindActorBodyType(archetype->physicsArchetype);
		if (bodyType == px::eBodyType::None)
			return { .result = eActorLifecycleResult::InvalidRequest, .action = eActorAction::Spawn };

		const ActorId actorId(instance.actorId);
		if (!actorId.IsValid() || m_directory.Resolve(actorId) != entt::null)
		{
			JAM_LOG_WARN("[ServerActorLifecycleSystem::PrepareLevelSpawn] invalid or duplicate actorId={}", instance.actorId);
			return { .result = eActorLifecycleResult::InvalidRequest, .action = eActorAction::Spawn };
		}

		params = SpawnParams{};
		params.actorArchetypeKey = archetype->key;
		params.desc.archetype = archetype->physicsArchetype;
		params.desc.pose = instance.pose;
		params.desc.spawnSrc = px::eSpawnSource::Level;
		params.desc.overrides = (bodyType == px::eBodyType::Character)
			? std::variant<px::RigidSpawnOverrides, px::CharacterSpawnOverrides>(px::CharacterSpawnOverrides{})
			: std::variant<px::RigidSpawnOverrides, px::CharacterSpawnOverrides>(px::RigidSpawnOverrides{});

		const auto result = PrepareActor(params, *archetype, false, actorId);
		if (result.Succeeded())
		{
			if (bodyType == px::eBodyType::Character)
				m_registry.get<CharAuthorityState>(result.entity).state.pos = instance.pose.p;
			else
				m_registry.get<RigidAuthorityState>(result.entity).state.pose = instance.pose;
		}
		return result;
	}

	ActorLifecycleResult ServerActorLifecycleSystem::PrepareActor(const SpawnParams& params, const ActorArchetypeData& archetype, bool clientRequest, ActorId actorId)
	{
		const entt::entity e = m_registry.create();
		if (actorId.IsValid())
		{
			if (!m_directory.Bind(actorId, e))
			{
				m_registry.destroy(e);
				return { .result = eActorLifecycleResult::Failed, .action = eActorAction::Spawn };
			}
		}
		else
			actorId = m_directory.Allocate(e);
		if (!actorId.IsValid())
		{
			m_registry.destroy(e);
			return { .result = eActorLifecycleResult::Failed, .action = eActorAction::Spawn };
		}

		m_registry.emplace<ActorId>(e, actorId);
		if (clientRequest)
			m_registry.emplace<OwnershipTag>(e, OwnershipTag{ params.owner });
		const px::eBodyType bodyType = params.desc.IsRigid() ? px::eBodyType::Rigid : px::eBodyType::Character;
		m_registry.emplace<ActorBodyType>(e, ActorBodyType{ bodyType });
		m_registry.emplace<ActorArchetypeRef>(e, ActorArchetypeRef{ params.actorArchetypeKey });
		m_registry.emplace<PhysicsArchetypeRef>(e, PhysicsArchetypeRef{ params.desc.archetype });

		if (!archetype.allowReplication)
			m_registry.emplace<ReplicationDisabledTag>(e);
		else if (m_world.FindActorMotionType(params.desc.archetype) == px::eMotionType::Static)
			m_registry.emplace<ReplicationStaticTag>(e);

		if (bodyType == px::eBodyType::Rigid)
			m_registry.emplace<RigidAuthorityState>(e);
		else
			m_registry.emplace<CharAuthorityState>(e);

		m_registry.emplace<NewlyCreatedTag>(e);

		JAM_ASSERT(m_directory.Validate());
		return { .result = eActorLifecycleResult::Succeeded, .action = eActorAction::Spawn, .actorId = actorId, .entity = e };
	}

	ActorLifecycleResult ServerActorLifecycleSystem::PrepareDespawn(UserId requester, ActorId actorId)
	{
		const auto entity = m_directory.Resolve(actorId);
		if (entity == entt::null || !m_registry.valid(entity))
			return { .result = eActorLifecycleResult::AlreadyAbsent, .action = eActorAction::Despawn, .actorId = actorId };

		const auto* ownership = m_registry.try_get<OwnershipTag>(entity);
		if (requester != kInvalidUserId && (!ownership || ownership->userId != requester))
			return { .result = eActorLifecycleResult::NotAuthorized, .action = eActorAction::Despawn, .actorId = actorId };

		JAM_ASSERT(m_directory.Validate());
		return { .result = eActorLifecycleResult::Succeeded, .action = eActorAction::Despawn, .actorId = actorId, .entity = entity };
	}

	void ServerActorLifecycleSystem::ReleaseActor(ActorId actorId, entt::entity entity)
	{
		if (m_registry.valid(entity))
		{
			JAM_ASSERT(!m_registry.all_of<PhysicsSpawnedTag>(entity));
			m_registry.destroy(entity);
		}
		if (!m_directory.Release(actorId))
			m_directory.Unbind(actorId);
	}

	bool ServerActorLifecycleSystem::BindPlayer(UserId userId, entt::entity entity)
	{
		const auto* ownership = m_registry.try_get<OwnershipTag>(entity);
		const auto* body = m_registry.try_get<ActorBodyType>(entity);
		if (!ownership || ownership->userId != userId || !body || body->body != px::eBodyType::Character)
			return false;

		if (const auto* control = m_registry.try_get<ControlTag>(entity))
			return control->userId == userId;

		m_registry.emplace<ControlTag>(entity, ControlTag{ userId });
		m_registry.emplace_or_replace<px::CharacterMotorInput>(entity);
		return true;
	}

	bool ServerActorLifecycleSystem::UnbindPlayer(UserId userId, entt::entity entity)
	{
		if (entity == entt::null || !m_registry.valid(entity))
			return false;

		const auto* ownership = m_registry.try_get<OwnershipTag>(entity);
		const auto* control = m_registry.try_get<ControlTag>(entity);
		JAM_ASSERT(ownership && ownership->userId == userId && control && control->userId == userId);
		m_registry.remove<ControlTag, px::CharacterMotorInput>(entity);
		return true;
	}

	std::optional<ActorId> ServerActorLifecycleSystem::FindSpawnResult(UserId requester, ClientRequestId requestId) const
	{
		const auto user = m_actorSpawnResults.find(requester);
		if (user == m_actorSpawnResults.end()) return std::nullopt;
		const auto result = user->second.find(requestId);
		if (result == user->second.end()) return std::nullopt;
		return result->second.actorId;
	}

	void ServerActorLifecycleSystem::RecordSpawnResult(UserId requester, ClientRequestId requestId, ActorId actorId)
	{
		auto& results = m_actorSpawnResults[requester];
		if (results.contains(requestId)) return;
		// Retain terminal results only, bounded per requester.
		constexpr size_t kMaxRetainedActorSpawnResults = 1024;
		if (results.size() >= kMaxRetainedActorSpawnResults)
		{
			auto oldest = results.begin();
			for (auto it = results.begin(); it != results.end(); ++it)
				if (it->second.completedAt < oldest->second.completedAt)
					oldest = it;
			results.erase(oldest);
		}
		results.emplace(requestId, ActorSpawnResult{ .actorId = actorId, .completedAt = NOW() });
	}

	void ServerActorLifecycleSystem::RemoveUser(UserId userId)
	{
		m_actorSpawnResults.erase(userId);
	}

	px::ActorId ServerActorLifecycleSystem::ResolveBindingTarget(ActorId targetActorId) const
	{
		if (!targetActorId.IsValid())
			return px::INVALID_ACTOR_ID;

		const entt::entity target = m_directory.Resolve(targetActorId);
		if (target == entt::null || !m_registry.valid(target) || !m_registry.all_of<PhysicsSpawnedTag>(target))
			return px::INVALID_ACTOR_ID;

		return GetPhysicsActorId(m_registry, target);
	}

}
