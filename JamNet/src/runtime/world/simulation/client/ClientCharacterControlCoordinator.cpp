#include "pch.h"
#include "jamnet/runtime/world/simulation/client/ClientCharacterControlCoordinator.h"

#include "jamnet/runtime/world/simulation/client/ClientWorld.h"
#include "jamnet/runtime/world/simulation/common/ActorComponents.h"
#include "jamnet/runtime/world/simulation/client/ClientInputSystem.h"

#include "jamnet/core/executor/GlobalEventBus.h"

#include <jampx/PhysicsFacade.h>

namespace jam::net
{

	void ClientCharacterControlCoordinator::Submit(CharacterControlIntent intent)
	{
		JAM_ASSERT(m_world.IsCurrentShardContext());
		if (intent.controlRevision == 0)
		{
			intent.controlRevision = ++m_controlRevision;
			if (intent.controlRevision == 0)
				intent.controlRevision = ++m_controlRevision;
		}
		else
		{
			m_controlRevision = intent.controlRevision;
		}
		if (std::holds_alternative<MoveByWorldRayIntent>(intent.locomotion))
			ResolveWorldRay(intent);

		m_currentIntent = std::move(intent);
		ApplyCurrentIntent();
	}

	void ClientCharacterControlCoordinator::ResolveWorldRay(CharacterControlIntent& intent)
	{
		const auto& ray = std::get<MoveByWorldRayIntent>(intent.locomotion);
		if (!m_world.m_physics)
		{
			intent.locomotion = StopMovementIntent{};
			return;
		}

		const px::HitscanResult hit = m_world.m_physics->Hitscan(
			ray.rayOrigin,
			ray.rayDirection.GetNormalized(),
			ray.maxRange);

		WorldRayResolvedEvent event
		{
			.accountId = m_world.GetAccountId(),
			.userId = m_world.GetUserId(),
			.worldId = m_world.GetWorldRef().worldId,
			.hit = hit.hit,
			.position = hit.position,
			.normal = hit.normal,
			.hitActorId = ActorId(hit.hitActorId),
		};
		GLOBAL_EVENTBUS_PUBLISH(event);

		if (!hit.hit)
		{
			intent.locomotion = StopMovementIntent{};
			return;
		}

		if (hit.hitActorId != px::INVALID_ACTOR_ID)
		{
			const entt::entity entity = m_world.ResolveActor(ActorId(hit.hitActorId));
			if (entity != entt::null
				&& m_world.m_registry.valid(entity)
				&& m_world.m_registry.all_of<ActorId>(entity)
				&& !m_world.m_registry.all_of<ReplicationStaticTag>(entity))
			{
				intent.locomotion = FollowActorIntent{ .target = m_world.m_registry.get<ActorId>(entity) };
				return;
			}
		}

		intent.locomotion = MoveToPositionIntent{ .target = hit.position };
	}

	void ClientCharacterControlCoordinator::ApplyCurrentIntent()
	{
		if (auto* inputSystem = m_world.m_registry.ctx().find<ClientInputSystem>())
			inputSystem->SetIntent(m_currentIntent);
	}
}
