#pragma once

#include "jamnet/runtime/world/lifecycle/WorldIdentity.h"
#include "jamnet/runtime/session/ClientRequestId.h"
#include "jamnet/runtime/world/lifecycle/WorldSyncTypes.h"

#include <functional>
#include <optional>
#include <string>
#include <variant>

namespace jam::net
{
	using WorldStateRevision = uint64;

	struct WorldTransitionToken
	{
		uint64 value = 0;
		bool IsValid() const noexcept { return value != 0; }
		auto operator<=>(const WorldTransitionToken&) const = default;
	};

	struct WorldTransitionTokenHash
	{
		size_t operator()(WorldTransitionToken token) const noexcept
		{
			return std::hash<uint64>{}(token.value);
		}
	};

	enum class eWorldDestinationSelector : uint8
	{
		DefaultForArchetype = 0,
		ExplicitInstance	= 1,
		AuthoredDestination = 2,
	};

	struct EnterWorldRequest
	{
		ClientRequestId				requestId				= kInvalidClientRequestId;
		WorldArchetypeKey			archetypeKey			= {};
		eWorldDestinationSelector	selector				= eWorldDestinationSelector::DefaultForArchetype;
		WorldInstanceId				explicitInstanceId		= kInvalidWorldInstanceId;
		std::string					destinationName;
		std::string					contentEntryPoint;
		WorldStateRevision			expectedMainRevision	= 0;

		bool IsServerResolved() const noexcept
		{
			return !IsValidAssetKey(archetypeKey)
				&& selector == eWorldDestinationSelector::DefaultForArchetype
				&& !explicitInstanceId.IsValid()
				&& destinationName.empty()
				&& contentEntryPoint.empty();
		}

		bool IsValid() const noexcept
		{
			if (IsServerResolved())
				return true;
			if (!IsValidAssetKey(archetypeKey))
				return false;
			if (selector == eWorldDestinationSelector::ExplicitInstance)
				return explicitInstanceId.IsValid();
			if (selector == eWorldDestinationSelector::AuthoredDestination)
				return !destinationName.empty();
			return selector == eWorldDestinationSelector::DefaultForArchetype;
		}
	};

	struct LeaveWorldRequest
	{
		ClientRequestId		requestId = kInvalidClientRequestId;
		WorldStateRevision	expectedMainRevision = 0;
	};

	// Client-facing World request envelope. The transport owner selects the
	// concrete Enter/Leave path; callers do not choose a legacy action system.
	struct WorldActionCommand
	{
		std::variant<EnterWorldRequest, LeaveWorldRequest> payload;
	};

	enum class eWorldTransitionFailure : uint8
	{
		None					= 0,
		InvalidRequest			= 1,
		StaleRevision			= 2,
		DestinationUnavailable	= 3,
		CapacityExceeded		= 4,
		ClientPrepareFailed		= 5,
		Timeout					= 6,
		Disconnected			= 7,
		WorldDestroyed			= 8,
		InternalError			= 9,
	};

	// Single authoritative Main PhysicalWorld state. The same value type is
	// stored in UserContext and copied into responses/notifications.
	struct UserWorldState
	{
		std::optional<WorldRef>	main	 = std::nullopt;
		WorldStateRevision				revision = 0;

		bool IsValid() const noexcept { return !main || main->IsValid(); }

		bool Matches(const WorldRef& world, WorldStateRevision expectedRevision) const noexcept
		{
			return main && *main == world && revision == expectedRevision;
		}

		bool SetMain(const WorldRef& value)
		{
			if (!value.IsValid() || (main && *main == value))
				return false;
			main = value;
			++revision;
			return true;
		}

		bool ClearIfWorld(WorldId worldId)
		{
			if (!main || main->worldId != worldId)
				return false;
			main.reset();
			++revision;
			return true;
		}
	};

	enum class eWorldTransitionKind : uint8
	{
		Enter = 0,
		Leave,
	};

	struct WorldTransitionResult
	{
		eWorldTransitionKind		kind			= eWorldTransitionKind::Enter;
		ClientRequestId				requestId		= kInvalidClientRequestId;
		WorldTransitionToken		transitionToken	= {};
		eWorldTransitionFailure		failure			= eWorldTransitionFailure::None;
		UserWorldState		state			= {};
	};

	struct WorldEventCorrelation
	{
		WorldRef		world		 = {};
		WorldStateRevision	mainRevision = 0;
	};

	struct ClientWorldPrepare
	{
		WorldSyncToken			token		    = {};
		eWorldSyncKind			kind			= eWorldSyncKind::WorldPrepare;
		WorldEventCorrelation	correlation		= {};
		WorldArchetypeKey		archetypeKey	= {};
		uint64					contentRevision = 0;
	};

	struct ClientWorldSyncResult
	{
		WorldSyncToken			token	  = {};
		bool					succeeded = false;
		eWorldTransitionFailure failure	  = eWorldTransitionFailure::None;
	};

	struct ClientWorldCommit
	{
		WorldSyncToken			token = {};
		WorldEventCorrelation	correlation = {};
	};

	enum class eWorldTransitionPhase : uint8
	{
		None = 0,
		ResolvingTarget,
		// No destination reservation or separate content activation phase.
		DetachingMembership = 12,
		JoiningMembership,
		AcceptingMembership,
		FinalizingMembershipLeave,
		CancellingMembership,
		RecoveringMembership,
	};

	// Disconnect uses the Leave flow, retries cleanup, and sends no client result.
	enum class eWorldTransitionReason : uint8
	{
		Request,
		Disconnect,
	};

	// None means idle. Finish a transition by resetting the entire state.
	struct WorldTransitionState
	{
		WorldTransitionToken				token			 = {};
		eWorldTransitionKind				kind			 = eWorldTransitionKind::Enter;
		uint64								userId			 = 0;
		ClientRequestId						requestId		 = kInvalidClientRequestId;
		eWorldTransitionPhase				phase			 = eWorldTransitionPhase::None;
		std::optional<WorldRef>				source			 = std::nullopt;
		std::optional<WorldRef>				target			 = std::nullopt;
		WorldInstanceRef					targetInstance	 = {};
		WorldStateRevision					expectedRevision = 0;
		Timestamp							deadline		 = Timestamp::Zero();
		eWorldTransitionFailure				failure			 = eWorldTransitionFailure::None;
		eWorldTransitionReason				reason			 = eWorldTransitionReason::Request;
		bool								inFlight		 = false;							// Waiting for the current phase's submitted World-shard command result.
		Timestamp							retryAt			 = Timestamp::Zero();				// Earliest time to retry the current phase; zero means no scheduled retry.
		std::string							contentEntryPoint;

		bool IsActive() const noexcept
		{
			return phase != eWorldTransitionPhase::None;
		}

		bool IsCommitted() const noexcept
		{
			return phase == eWorldTransitionPhase::AcceptingMembership
				|| phase == eWorldTransitionPhase::FinalizingMembershipLeave;
		}

		bool IsCleanupPending() const noexcept
		{
			return IsCommitted()
				|| phase == eWorldTransitionPhase::CancellingMembership
				|| phase == eWorldTransitionPhase::RecoveringMembership;
		}
	};

	struct WorldTransitionContinuation
	{
		uint64					userId			= 0;
		WorldTransitionToken	token			= {};
		eWorldTransitionPhase	expectedPhase	= eWorldTransitionPhase::ResolvingTarget;
	};
}
