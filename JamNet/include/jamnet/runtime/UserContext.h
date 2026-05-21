#pragma once

#include "jamnet/core/executor/ShardOwnedObject.h"
#include "jamnet/runtime/world/WorldActionTypes.h"

#include <optional>
#include <unordered_map>
#include <vector>

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


	struct InFlightWorldAction
	{
		uint64									txnId = 0;
		WorldActionRequest						request = {};
		WorldActionPlan							plan = {};
		std::optional<WorldMembership>			previousSourceMembership = {};
		std::optional<WorldMembership>			previousTargetMembership = {};
		uint64									startedNs = 0;
	};

	struct UserContext
	{
		AccountId						accountId	= kInvalidAccountId;
		UserId							userId		= kInvalidUserId;
		SessionId						tcp			= kInvalidSessionId;
		SessionId						udp			= kInvalidSessionId;
		std::vector<WorldMembership>	worlds; // joined worlds only
		std::unordered_map<uint64, InFlightWorldAction> inFlightWorldActions;
	};


	struct UserShardState
	{
		uint16									shardIndex = 0;
		RuntimeSlotTable<UserContext>			usersById;
		std::unordered_map<AccountId, UserId>	accountUsers;

		UserContext*		AllocUserContext(AccountId accountId);
		void				FreeUserContext(UserId userId);
		UserContext*		FindUserContext(UserId userId);
		const UserContext*	FindUserContext(UserId userId) const;
		UserContext*		FindUserContextByAccount(AccountId accountId);
		const UserContext*	FindUserContextByAccount(AccountId accountId) const;
		UserContext*		FindPrimaryUserContext();
		const UserContext*	FindPrimaryUserContext() const;
		UserContext*		EnsurePrimaryUserContext(AccountId accountId);
		UserContext*		EnsureUserContext(AccountId accountId);
		void				RemovePrimaryUserContext(AccountId accountId);
	};


	UserShardState& GetOrCreateUserShardState(ShardLocal& local);
}
