#pragma once

#include "jamnet/runtime/world/lifecycle/WorldIdentity.h"
#include "jamnet/runtime/session/ClientRequestId.h"
#include "jamnet/runtime/protocol/transport/WireBarrier.h"

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

		bool IsValid() const noexcept
		{
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
		RuntimeDestroyed		= 8,
		InternalError			= 9,
	};

	// Single authoritative Main PhysicalWorld state. The same value type is
	// stored in UserContext and copied into responses/notifications.
	struct UserPhysicalWorldState
	{
		std::optional<WorldRuntimeRef>	main	 = std::nullopt;
		WorldStateRevision				revision = 0;

		bool IsValid() const noexcept { return !main || main->IsValid(); }

		bool SetMain(const WorldRuntimeRef& value)
		{
			if (!value.IsValid() || (main && *main == value))
				return false;
			main = value;
			++revision;
			return true;
		}

		bool ClearIfRuntime(WorldId runtimeId)
		{
			if (!main || main->worldId != runtimeId)
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
		UserPhysicalWorldState		state			= {};
	};

	struct WorldEventCorrelation
	{
		WorldRuntimeRef		world		 = {};
		WorldStateRevision	mainRevision = 0;
	};

	enum class eWireBarrierKind : uint8
	{
		WorldPrepare		= 0,
		WorldResync			= 1,
		ReplicationBaseline	= 2,
		WorldContent		= 3,
	};

	struct ClientWorldPrepare
	{
		WireBarrierToken		token		    = {};
		eWireBarrierKind		kind			= eWireBarrierKind::WorldPrepare;
		WorldEventCorrelation	correlation		= {};
		WorldArchetypeKey		archetypeKey	= {};
		uint64					contentRevision = 0;
	};

	struct ClientBarrierResult
	{
		WireBarrierToken		token	  = {};
		bool					succeeded = false;
		eWorldTransitionFailure failure	  = eWorldTransitionFailure::None;
	};

	struct ClientWorldCommit
	{
		WireBarrierToken		token = {};
		WorldEventCorrelation	correlation = {};
	};

	enum class eWorldTransitionPhase : uint8
	{
		ResolvingTarget			= 0,
		ReservingTarget			= 1,
		WaitingClientPrepared	= 2,
		DetachingSource			= 3,
		AttachingTarget			= 4,
		CommittingMain			= 5,
		ActivatingTarget		= 6,
		CommittingSourceLeave	= 7,
		WaitingClientApplied	= 8,
		RollingBackTarget		= 9,
		RestoringSource			= 10,
		PreparingTargetContent	= 11,
	};

	struct WorldTransitionState
	{
		WorldTransitionToken				token			 = {};
		eWorldTransitionKind				kind			 = eWorldTransitionKind::Enter;
		uint64								userId			 = 0;
		ClientRequestId						requestId		 = kInvalidClientRequestId;
		eWorldTransitionPhase				phase			 = eWorldTransitionPhase::ResolvingTarget;
		std::optional<WorldRuntimeRef>		source			 = std::nullopt;
		std::optional<WorldRuntimeRef>		target			 = std::nullopt;
		WorldInstanceRef					targetInstance	 = {};
		WorldStateRevision					expectedRevision = 0;
		WireBarrierToken					barrierToken	 = {};
		uint64								deadlineNs		 = 0;
		eWorldTransitionFailure				terminalFailure  = eWorldTransitionFailure::None;
		bool								sourceDetached	 = false;
		std::string							contentEntryPoint;
	};

	struct WorldTransitionContinuation
	{
		uint64					userId			= 0;
		WorldTransitionToken	token			= {};
		eWorldTransitionPhase	expectedPhase	= eWorldTransitionPhase::ResolvingTarget;
	};
}
