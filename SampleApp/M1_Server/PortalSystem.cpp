#include "pch.h"
#include "PortalSystem.h"

#include <jamnet/runtime/world/simulation/common/ActorComponents.h>
#include <jamnet/runtime/world/simulation/server/ServerWorld.h>

namespace m1
{
	PortalSystem::PortalSystem(std::vector<PortalDefinition> definitions)
		: m_definitions(std::move(definitions))
	{
	}

	bool PortalSystem::Initialize(jam::net::ServerWorld& world)
	{
		auto& registry = world.GetRegistry();
		const auto sourceArchetypeKey = world.GetWorldInstance().archetypeKey;

		for (const PortalDefinition& definition : m_definitions)
		{
			if (definition.sourceWorldArchetypeKey != sourceArchetypeKey)
				continue;
			if (!definition.actorId.IsValid()
				|| !jam::IsValidAssetKey(definition.portal.destinationArchetypeKey))
			{
				return false;
			}

			const entt::entity portal = world.ResolveActor(definition.actorId);
			if (portal == entt::null || !registry.valid(portal))
				return false;

			registry.emplace_or_replace<PortalComponent>(portal, definition.portal);
		}

		return true;
	}

	void PortalSystem::OnPhysicsEvents(jam::net::ServerWorld& world, std::span<const jam::px::PhysicsEvent> events) const
	{
		for (const jam::px::PhysicsEvent& event : events)
		{
			if (event.type != jam::px::ePhysicsEventType::TriggerFound)
				continue;

			TryEnterPortal(world, event.triggerActorId, event.otherActorId);
		}
	}

	bool PortalSystem::TryEnterPortal(jam::net::ServerWorld& world, jam::px::ActorId triggerActorId, jam::px::ActorId otherActorId) const
	{
		if (triggerActorId == jam::px::INVALID_ACTOR_ID || otherActorId == jam::px::INVALID_ACTOR_ID)
		{
			return false;
		}

		auto& registry = world.GetRegistry();
		const entt::entity portalEntity = world.ResolveActor(jam::net::ActorId(triggerActorId));
		const entt::entity otherEntity  = world.ResolveActor(jam::net::ActorId(otherActorId));
		
		if (portalEntity == entt::null || otherEntity == entt::null || !registry.valid(portalEntity) || !registry.valid(otherEntity))
		{
			return false;
		}

		const auto* portal  = registry.try_get<PortalComponent>(portalEntity);
		const auto* control = registry.try_get<jam::net::ControlTag>(otherEntity);
		
		if (!portal || !control || control->userId == jam::net::kInvalidUserId || world.GetControlledEntity(control->userId) != otherEntity)
		{
			return false;
		}

		jam::net::EnterWorldRequest request{};
		request.archetypeKey		= portal->destinationArchetypeKey;
		request.selector			= portal->selector;
		request.explicitInstanceId	= portal->explicitInstanceId;
		request.destinationName		= portal->destinationName;
		request.contentEntryPoint	= portal->arrivalSpawn;
		
		return world.RequestEnterWorld(control->userId, std::move(request));
	}
}
