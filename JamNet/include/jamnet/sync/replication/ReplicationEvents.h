// Deprecated: legacy replication/app bridge types.
// New app-layer code should use jamnet/runtime/AppRuntimeEvents.h and ClientRuntime polling APIs instead.
#pragma once
#include "jamnet/runtime/actor/ActorArchetypeDatabase.h"
#include "jamnet/runtime/world/types/WorldActionTypes.h"

#include <jampx/PhysicsTypes.h>

namespace jam::net
{
	enum class eRenderActorLifecycleReason : uint8
	{
		Created,
		AoiEntered,
		AoiLeft,
		Destroyed,
		PredictedDespawn
	};

	struct ClientTcpBoundEvent
	{
		uint64	accountId = 0;
		uint64	userId = 0;
	};

	struct ClientUdpBoundEvent
	{
		uint64	accountId = 0;
		uint64	userId = 0;
	};

	struct ClientSessionReadyEvent
	{
		uint64	accountId	= 0;
		uint64	userId		= 0;
		bool	tcpBound	= false;
		bool	udpBound	= false;
		bool	ready		= false;
	};

	struct ClientBindStateChangedEvent
	{
		uint64			accountId	= 0;
		uint64			userId		= 0;
		ClientBindState	state		= {};
	};

	struct WorldRequestResultEvent
	{
		uint64					accountId		= 0;
		uint64					userId			= 0;
		eWorldActionStatus		status			= eWorldActionStatus::Failed;
		eWorldAction			requestAction	= eWorldAction::AutoAssign;
		eWorldActionReason		reason			= eWorldActionReason::None;
		eWorldRole			localRole		= eWorldRole::None;
		WorldKey				sourceWorldKey	= {};
		WorldKey				targetWorldKey	= {};

		bool Succeeded() const { return status == eWorldActionStatus::Succeeded; }
		bool Failed() const { return status == eWorldActionStatus::Failed; }
		bool IsAssigned() const { return Succeeded() && targetWorldKey.IsValid(); }
	};

	struct WorldAssignmentSucceededEvent
	{
		uint64				accountId		= 0;
		uint64				userId			= 0;
		eWorldRole		localRole		= eWorldRole::None;
		WorldKey			worldKey		= {};
	};

	struct WorldActionNotificationEvent
	{
		uint64					accountId		= 0;
		uint64					userId			= 0;
		eWorldActionStatus		status			= eWorldActionStatus::Failed;
		eWorldAction			requestAction	= eWorldAction::AutoAssign;
		eWorldActionReason		reason			= eWorldActionReason::None;
		eWorldRole			localRole		= eWorldRole::None;
		WorldKey				sourceWorldKey	= {};
		WorldKey				targetWorldKey	= {};
	};

	struct RenderActorSpawnedEvent
	{
		uint64			accountId	= 0;
		uint64			userId		= 0;
		uint32			spawnReqId	= 0;
		uint32			netId		= 0;
		uint32			objectId	= 0;
		bool			isLocal		= false;
		eRenderActorLifecycleReason reason = eRenderActorLifecycleReason::Created;
		ActorArchetypeKey		actorArchetypeKey = {};
	};

	struct RenderLevelSpawnedEvent
	{
		uint64	accountId = 0;
		uint64	userId = 0;
		std::unordered_map<px::ObjectId, px::PhysicsArchetypeKey> instances;
	};

	struct RenderActorDespawnedEvent
	{
		uint64	accountId	= 0;
		uint64	userId		= 0;
		uint32	netId		= 0;
		uint32	objectId	= 0;
		eRenderActorLifecycleReason reason = eRenderActorLifecycleReason::Destroyed;
	};

	struct RenderSamplesEvent
	{
		struct ActorSample
		{
			uint32							objectId = 0;
			bool							isLocal = false;
			std::optional<px::RigidState>		rs{};
			std::optional<px::CharacterState>	cs{};
			std::optional<px::CharacterState>	csRaw{};
		};

		uint64						accountId = 0;
		uint32						tick	  = 0;
		uint64						userId	  = 0;
		float						timestamp = 0.f;
		std::vector<ActorSample>	actors;
	};
}
