#pragma once

#include <jampx/PhysicsTypes.h>

#include "jamnet/runtime/world/WorldAssignmentTypes.h"

namespace jam::net
{
	struct ClientTcpBoundEvent
	{
		uint64	userId = 0;
	};

	struct ClientUdpBoundEvent
	{
		uint64	userId = 0;
	};

	struct ClientSessionReadyEvent
	{
		uint64	userId		= 0;
		bool	tcpBound	= false;
		bool	udpBound	= false;
		bool	ready		= false;
	};

	struct ClientBindStateChangedEvent
	{
		uint64			userId		= 0;
		ClientBindState	state		= {};
	};

	struct WorldRequestResultEvent
	{
		uint64					userId				= 0;
		eWorldAssignmentStatus	status				= eWorldAssignmentStatus::Failed;
		eWorldRequestAction		requestAction		= eWorldRequestAction::AutoAssign;
		eWorldAssignmentAction	assignmentAction	= eWorldAssignmentAction::None;
		eWorldTransferReason	reason				= eWorldTransferReason::None;
		WorldId					worldId				= INVALID_WORLD_ID;

		bool IsAssigned() const { return status == eWorldAssignmentStatus::Assigned; }
		bool IsWaiting()  const { return status == eWorldAssignmentStatus::Waiting; }
		bool IsFailed()   const { return status == eWorldAssignmentStatus::Failed; }
	};

	struct WorldAssignmentSucceededEvent
	{
		uint64	userId  = 0;
		uint32	worldId = 0;
	};

	struct RenderActorSpawnedEvent
	{
		uint64			userId		= 0;
		uint32			spawnReqId	= 0;
		uint32			objectId	= 0;
		bool			isLocal		= false;
		px::PrefabKey	prefab		= {};
	};

	struct RenderLevelSpawnedEvent
	{
		uint64	userId = 0;
		std::unordered_map<px::ObjectId, px::PrefabKey> instances;
	};

	struct RenderActorDespawnedEvent
	{
		uint64	userId	 = 0;
		uint32	objectId = 0;
	};

	struct ClickMoveResolvedEvent
	{
		uint64		userId		 = 0;
		uint64		requestSeq	 = 0;
		bool		followTarget = false;
		px::Vec3	targetPos	 = px::Vec3::Zero();
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

		uint32						tick	  = 0;
		uint64						userId	  = 0;
		float						timestamp = 0.f;
		std::vector<ActorSample>	actors;
	};
}
