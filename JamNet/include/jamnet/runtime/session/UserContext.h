#pragma once

#include "jamnet/core/executor/ShardOwnedObject.h"
#include "jamnet/runtime/world/lifecycle/WorldTransitionTypes.h"

#include <unordered_map>

namespace jam
{
	struct ShardLocal;
}

namespace jam::net
{

	using AccountId = uint64;
	inline constexpr AccountId kInvalidAccountId = 0;

	using UserId = RuntimeId;
	inline constexpr UserId kInvalidUserId = kInvalidRuntimeId;

	inline UserId MakeUserId(uint16 shardIndex, uint16 localIndex, uint32 generation)
	{
		return MakeRuntimeId(shardIndex, localIndex, generation);
	}

	inline uint16 GetUserShardIndex(UserId id)
	{
		return GetRuntimeShardIndex(id);
	}

	inline uint16 GetUserLocalIndex(UserId id)
	{
		return GetRuntimeLocalIndex(id);
	}

	inline uint32 GetUserGeneration(UserId id)
	{
		return GetRuntimeGeneration(id);
	}

	enum class eUserConnectionState : uint8
	{
		Connected,
		Reconnecting,
		Released,
	};

	enum class eUserWorldSyncPhase : uint8
	{
		None,
		WaitingTransition,
		WaitingClientPrepared,
		WaitingClientApplied,
		WaitingTransport,
		WaitingActivation,
	};

	struct UserWorldSyncContext
	{
		eUserWorldSyncPhase		phase			= eUserWorldSyncPhase::None;
		WorldSyncToken			token			= {};
		WorldRef				world			= {};
		WorldStateRevision		mainRevision	= 0;
		Timestamp				deadline		= Timestamp::Zero();
		Timestamp				retryAt			= Timestamp::Zero();

		bool IsActive() const noexcept
		{
			return phase != eUserWorldSyncPhase::None;
		}

		bool MatchesRequest(const UserWorldSyncContext& request) const noexcept
		{
			return token == request.token
				&& world == request.world
				&& mainRevision == request.mainRevision;
		}
	};

	// User is one client identity with its TCP/UDP sessions; replacing a session
	// does not by itself replace the User. A User has at most one committed World membership.
	struct UserContext
	{
		AccountId						accountId			= kInvalidAccountId;
		UserId							userId				= kInvalidUserId;
		SessionId						tcp					= kInvalidSessionId;
		SessionId						udp					= kInvalidSessionId;
		eUserConnectionState			connectionState		= eUserConnectionState::Connected;
		Timestamp						reconnectDeadline   = Timestamp::Zero();
		UserWorldSyncContext			worldSync;
		UserWorldState					worldState;
		WorldTransitionState			worldTransition;

		bool IsConnected() const noexcept
		{
			return connectionState == eUserConnectionState::Connected;
		}

		// Session registration only; does not inspect live transport objects.
		bool HasBothSessions() const noexcept
		{
			return tcp != kInvalidSessionId && udp != kInvalidSessionId;
		}

		bool HasNoSessions() const noexcept
		{
			return tcp == kInvalidSessionId && udp == kInvalidSessionId;
		}

		bool HasPendingWorldWork() const noexcept
		{
			return worldTransition.IsActive() || worldSync.IsActive();
		}

		bool CanRelease() const noexcept
		{
			return connectionState == eUserConnectionState::Released
				&& HasNoSessions()
				&& !worldState.main
				&& !worldTransition.IsActive();
		}
	};

	struct UserShardState
	{
		uint16									shardIndex = 0;
		RuntimeSlotTable<UserContext>			usersById;
		std::unordered_map<AccountId, UserId>	accountUsers;

		UserContext*		AllocUserContext(AccountId accountId);
		void				FreeUserContext(UserId userId);
		void				ReleaseAccountBinding(UserId userId);
		
		UserContext*		FindUserContext(UserId userId);
		const UserContext*	FindUserContext(UserId userId) const;
		UserContext*		FindUserContextByAccount(AccountId accountId);
		const UserContext*	FindUserContextByAccount(AccountId accountId) const;
		
		UserContext*		EnsureUserContext(AccountId accountId);
	};


	UserShardState& GetOrCreateUserShardState(ShardLocal& local);
}
