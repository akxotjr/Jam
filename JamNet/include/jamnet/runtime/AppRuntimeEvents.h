#pragma once

#include "jamnet/runtime/world/types/WorldActionTypes.h"
#include "jamnet/sync/replication/NetActorComponents.h"

#include <jampx/PhysicsTypes.h>

#include <optional>
#include <span>


namespace jam::net
{

	enum class eNetworkPhase : uint8
	{
		Disconnected,
		Connecting,
		Ready,
		Degraded,
	};

	struct NetworkState
	{
		eNetworkPhase	phase	= eNetworkPhase::Disconnected;
	};

	struct NetworkStateEvent
	{
		uint64			accountId = 0;
		uint64			userId	 = 0;
		NetworkState	state	 = {};
	};

	enum class eWorldMembershipChange : uint8
	{
		Joined,
		Left,
		Promoted,
		Transferred,
		Updated,
	};

	enum class eWorldParticipantChange : uint8
	{
		Joined,
		Left,
	};

	struct WorldMembershipView
	{
		WorldKey					key			 = {};
		LocalWorldId				localWorldId = kInvalidLocalWorldId;
		eWorldKind					kind		 = eWorldKind::Virtual;
		eWorldRole					role		 = eWorldRole::None;

		bool IsValid() const { return key.IsIssued(); }
	};

	struct WorldMembershipEvent
	{
		uint64					accountId  = 0;
		uint64					userId	   = 0;
		eWorldMembershipChange	change	   = eWorldMembershipChange::Updated;
		WorldMembershipView		membership = {};
	};

	struct WorldParticipantView
	{
		WorldKey		key					= {};
		LocalWorldId	localWorldId		= kInvalidLocalWorldId;
		eWorldKind		kind				= eWorldKind::Virtual;
		uint64			participantUserId	= 0;

		bool IsValid() const { return key.IsIssued() && localWorldId != kInvalidLocalWorldId && participantUserId != 0; }
	};

	struct WorldParticipantEvent
	{
		uint64					accountId	 = 0;
		uint64					userId		 = 0;
		eWorldParticipantChange	change		 = eWorldParticipantChange::Joined;
		WorldParticipantView	participant	 = {};
	};

	enum class eActorLifecycleReason : uint8
	{
		Spawned,
		Despawned,
		AoiEntered,
		AoiLeft,
		PredictedDespawn,
	};

	struct ActorLifecycleEvent
	{
		uint64					accountId = 0;
		uint64					userId	  = 0;
		LocalWorldId			localWorldId = kInvalidLocalWorldId;
		uint32					spawnReqId = 0;
		NetId					netId	  = NetId::Invalid();
		uint32					objectId  = 0;
		bool					isLocal	  = false;
		eActorLifecycleReason	reason	  = eActorLifecycleReason::Spawned;
		ActorArchetypeKey		actorArchetypeKey = {};
	};

	struct ClickMoveResolvedEvent
	{
		uint64		accountId	 = 0;
		uint64		userId		 = 0;
		uint64		requestSeq	 = 0;
		bool		followTarget = false;
		px::Vec3	targetPos	 = px::Vec3::Zero();
	};

	// Render-ready presentation sample produced after client replay/correction is applied.
	struct ActorPresentationState
	{
		uint32								objectId = 0;
		NetId								netId	 = NetId::Invalid();
		bool								isLocal	 = false;
		std::optional<px::RigidState>		rs		 = {};
		std::optional<px::CharacterState>	cs	 = {};
		std::optional<px::CharacterState>	csRaw = {};
	};

	// Double-buffered presentation frame polled by the app layer at render cadence.
	struct ActorPresentationFrame
	{
		uint64								sequence = 0;
		uint32								tick = 0;
		float								timestamp = 0.f;
		std::vector<ActorPresentationState>	actors;
	};

	struct ActorPresentationFrameView
	{
		uint64								sequence  = 0;
		uint32								tick	  = 0;
		float								timestamp = 0.f;
		std::span<const ActorPresentationState>	actors = {};
	};

	// Internal bridge event used to push a completed presentation frame from client world systems into ClientRuntime.
	struct PresentationFramePushedEvent
	{
		uint64					accountId = 0;
		uint64					userId    = 0;
		NetWorldId				worldId   = kInvalidNetWorldId;
		ActorPresentationFrame	frame = {};
	};

	class IAppRuntimeEvents
	{
	public:
		virtual ~IAppRuntimeEvents() = default;
		virtual void OnNetworkStateEvent(const NetworkStateEvent& evt) {}
		virtual void OnWorldMembershipEvent(const WorldMembershipEvent& evt) {}
		virtual void OnWorldParticipantEvent(const WorldParticipantEvent& evt) {}
		virtual void OnActorLifecycleEvent(const ActorLifecycleEvent& evt) {}
	};
}
