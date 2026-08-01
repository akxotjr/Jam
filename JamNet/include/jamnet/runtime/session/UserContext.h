#pragma once

#include "jamnet/core/executor/ShardOwnedObject.h"
#include "jamnet/runtime/world/lifecycle/WorldTransitionTypes.h"

#include <optional>
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

	struct UserWorldTransitionContext
	{
		std::optional<WorldTransitionState> active;
	};

	struct UserContext
	{
		AccountId						accountId	= kInvalidAccountId;
		UserId							userId		= kInvalidUserId;
		SessionId						tcp			= kInvalidSessionId;
		SessionId						udp			= kInvalidSessionId;
		UserPhysicalWorldState			physicalWorld;
		UserWorldTransitionContext		worldTransition;
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
		
		UserContext*		EnsureUserContext(AccountId accountId);
	};


	UserShardState& GetOrCreateUserShardState(ShardLocal& local);
}
