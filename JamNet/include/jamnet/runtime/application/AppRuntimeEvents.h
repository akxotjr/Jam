#pragma once

#include "jamnet/core/net/PacketStructure.h"
#include "jamnet/runtime/world/lifecycle/WorldTransitionTypes.h"
#include "jamnet/runtime/world/actor/ActorActionTypes.h"
#include "jamnet/runtime/content/social/SocialTypes.h"
#include "jamnet/runtime/content/generic/GenericContentTypes.h"

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
		eNetworkPhase	phase = eNetworkPhase::Disconnected;
		eBootstrapKind	bootstrapKind = eBootstrapKind::Pending;
	};

	struct NetworkStateEvent
	{
		uint64			clientInstanceId = 0;
		uint64			accountId = 0;
		uint64			userId	 = 0;
		NetworkState	state	 = {};
	};

	enum class eWorldParticipantChange : uint8
	{
		Joined,
		Left,
	};

	struct WorldParticipantView
	{
		WorldRef	world				= {};
		uint64		participantUserId	= 0;

		bool IsValid() const { return world.IsValid() && participantUserId != 0; }
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
		LocallyHidden,
	};

	struct ActorLifecycleEvent
	{
		uint64					accountId = 0;
		uint64					userId	  = 0;
		WorldId					worldId   = kInvalidWorldId;
		ClientRequestId			clientRequestId = kInvalidClientRequestId;
		ActorId					actorId   = ActorId::Invalid();
		bool					isLocal	  = false;
		eActorLifecycleReason	reason	  = eActorLifecycleReason::Spawned;
		ActorArchetypeKey		actorArchetypeKey = {};
	};

	// Result of resolving a frontend point-and-click world ray in client physics.
	// This is emitted on the client-world shard after the synchronous query completes.
	struct WorldRayResolvedEvent
	{
		uint64		accountId	= 0;
		uint64		userId		= 0;
		WorldId		worldId		= kInvalidWorldId;
		bool		hit			= false;
		px::Vec3	position		= {};
		px::Vec3	normal			= {};
		ActorId		hitActorId	= ActorId::Invalid();
	};

	// Internal completion event for a client-originated Actor Action RPC.
	// requestId is client-local and is never sent to the server.
	struct ActorActionResultEvent
	{
		uint64				accountId	= 0;
		uint64				userId		= 0;
		ClientRequestId		requestId	= kInvalidClientRequestId;
		ActorActionResult	result		= {};
	};

	struct SocialMessageEvent
	{
		AccountId		accountId	= kInvalidAccountId;
		UserId			userId		= kInvalidUserId;
		SocialMessage	message		= {};
	};

	struct GenericContentResponseEvent
	{
		AccountId				accountId = kInvalidAccountId;
		UserId					userId	  = kInvalidUserId;
		GenericContentResponse	response  = {};
	};

	// Render-ready presentation sample produced after client replay/correction is applied.
	struct ActorPresentationState
	{
		ActorId								actorId  = ActorId::Invalid();
		bool								isLocal	 = false;
		std::optional<px::RigidState>		rs		 = {};
		std::optional<px::CharacterState>	cs		 = {};
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
		uint64									sequence	= 0;
		uint32									tick		= 0;
		float									timestamp	= 0.f;
		std::span<const ActorPresentationState>	actors		= {};
	};

	struct ActorPresentationFramePairView
	{
		ActorPresentationFrameView previous = {};
		ActorPresentationFrameView current  = {};
	};

	// Internal bridge event used to push a completed presentation frame from client world systems into ClientRuntime.
	struct PresentationFramePushedEvent
	{
		uint64					accountId = 0;
		uint64					userId    = 0;
		WorldId					worldId   = kInvalidWorldId;
		ActorPresentationFrame	frame	  = {};
	};

}
