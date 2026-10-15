#pragma once

#include "jamnet/runtime/world/actor/ActorActionTypes.h"
#include "jamnet/runtime/world/simulation/common/PhysicalWorld.h"
#include "jamnet/runtime/world/simulation/server/ActorLifecycleTypes.h"

#include <entt/fwd.hpp>
#include <optional>
#include <unordered_map>


namespace jam::net
{
	class ServerWorld;

	enum class eActorLifecycleResult : uint8
	{
		Succeeded,
		InvalidRequest,
		DuplicateInFlight,
		AlreadyPresent,
		AlreadyAbsent,
		NotAuthorized,
		NotImplemented,
		Failed,
	};

	struct ActorLifecycleResult
	{
		eActorLifecycleResult	result	= eActorLifecycleResult::Failed;
		eActorAction			action	= eActorAction::Spawn;
		ActorId					actorId = ActorId::Invalid();
		entt::entity			entity	= entt::null;

		bool Succeeded() const { return result == eActorLifecycleResult::Succeeded; }
	};

	class ServerActorLifecycleSystem
	{
	public:
		ServerActorLifecycleSystem(ServerWorld& world, entt::registry& registry, ActorDirectory& directory, const ActorArchetypeDatabase& archetypes);

		// Synchronous preparation/release only. ServerWorld owns ordering and completion.
		ActorLifecycleResult				PrepareSpawn(SpawnParams& params, bool clientRequest);
		ActorLifecycleResult				PrepareLevelSpawn(const ActorLevelInstanceData& instance, SpawnParams& params);
		ActorLifecycleResult				PrepareDespawn(UserId requester, ActorId actorId);
		void								ReleaseActor(ActorId actorId, entt::entity entity);

		bool								BindPlayer(UserId userId, entt::entity entity);
		bool								UnbindPlayer(UserId userId, entt::entity entity);

		std::optional<ActorId>				FindSpawnResult(UserId requester, ClientRequestId requestId) const;
		void								RecordSpawnResult(UserId requester, ClientRequestId requestId, ActorId actorId);
		void								RemoveUser(UserId userId);

	private:
		ActorLifecycleResult				PrepareActor(const SpawnParams& params, const ActorArchetypeData& archetype, bool clientRequest, ActorId actorId = ActorId::Invalid());
		px::ActorId							ResolveBindingTarget(ActorId targetActorId) const;

	private:
		ServerWorld&						m_world;
		entt::registry&						m_registry;
		ActorDirectory&						m_directory;
		const ActorArchetypeDatabase&		m_archetypes;

		struct ActorSpawnResult
		{
			ActorId actorId = ActorId::Invalid();
			Timestamp completedAt = Timestamp::Zero();
		};
		std::unordered_map<UserId, std::unordered_map<ClientRequestId, ActorSpawnResult>> m_actorSpawnResults;
	};
}
