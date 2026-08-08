#pragma once

#include "jamnet/runtime/session/ClientRequestId.h"
#include "jamnet/runtime/world/actor/ActorArchetypeDatabase.h"
#include "jamnet/runtime/world/actor/ActorId.h"
#include "jamnet/runtime/world/lifecycle/WorldIdentity.h"

#include <jampx/PhysicsTypes.h>

#include <optional>
#include <variant>

namespace jam::net
{
	// Frontend-visible spawn inputs. Physics asset selection and principal IDs are
	// intentionally absent; PhysicalWorld derives those internal values.
	struct FrontendSpawnActorSpec
	{
		ActorArchetypeKey	actorArchetypeKey{};
		px::Transform		pose = {};
		bool				requestOwnership = true;
		bool				requestControl = false;
		ActorId				targetActorId = ActorId::Invalid();

		std::optional<px::Vec3>		linearVelocity;
		std::optional<px::Vec3>		angularVelocity;
		std::optional<float>		linearDamping;
		std::optional<float>		angularDamping;
		std::optional<float>		viewYaw;
		std::optional<float>		viewPitch;
	};

	enum class eActorAction : uint8
	{
		Spawn,
		Despawn,
	};

	struct SpawnActorRequest
	{
		ClientRequestId			requestId = kInvalidClientRequestId;
		FrontendSpawnActorSpec	spec = {};
	};

	struct DespawnActorRequest
	{
		ClientRequestId	requestId = kInvalidClientRequestId;
		ActorId			actorId = ActorId::Invalid();
	};

	struct ActorActionCommand
	{
		// Invalid selects the current main physical world in ClientRuntime.
		WorldId		worldId = kInvalidWorldId;
		std::variant<SpawnActorRequest, DespawnActorRequest> payload = SpawnActorRequest{};
	};

	enum class eActorActionStatus : uint8
	{
		Succeeded,
		Failed,
	};

	enum class eActorActionReason : uint8
	{
		None,
		InvalidArgument,
		WorldUnavailable,
		ActorNotFound,
		TransportUnavailable,
		Rejected,
		Shutdown,
	};

	struct ActorActionResult
	{
		eActorActionStatus	status  = eActorActionStatus::Failed;
		eActorActionReason	reason  = eActorActionReason::None;
		eActorAction		action  = eActorAction::Spawn;
		ActorId				actorId = ActorId::Invalid();

		bool Succeeded() const { return status == eActorActionStatus::Succeeded; }
	};
}
