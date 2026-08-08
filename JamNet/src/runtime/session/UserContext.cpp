#include "pch.h"
#include "jamnet/core/executor/GlobalExecutor.h"
#include "jamnet/core/executor/ShardExecutor.h"
#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/core/net/SessionShardState.h"
#include "jamnet/runtime/session/UserContext.h"

namespace jam::net
{

	UserContext* UserShardState::AllocUserContext(AccountId accountId)
	{
		if (accountId == kInvalidAccountId)
			return nullptr;

		if (auto* existing = FindUserContextByAccount(accountId))
			return existing;

		const UserId userId = usersById.AllocId(shardIndex);
		if (userId == kInvalidUserId)
			return nullptr;

		UserContext* ctx = usersById.Find(userId);
		if (!ctx)
		{
			usersById.FreeId(userId);
			return nullptr;
		}

		ctx->userId = userId;
		ctx->accountId = accountId;
		accountUsers[accountId] = ctx->userId;
		return ctx;
	}

	void UserShardState::FreeUserContext(UserId userId)
	{
		UserContext* ctx = usersById.Find(userId);
		if (!ctx)
			return;

		ReleaseAccountBinding(userId);

		usersById.FreeId(userId);
	}

	void UserShardState::ReleaseAccountBinding(UserId userId)
	{
		const UserContext* ctx = usersById.Find(userId);
		if (!ctx || ctx->accountId == kInvalidAccountId)
			return;

		const auto it = accountUsers.find(ctx->accountId);
		if (it != accountUsers.end() && it->second == userId)
			accountUsers.erase(it);
	}

	UserContext* UserShardState::FindUserContext(UserId userId)
	{
		UserContext* ctx = usersById.Find(userId);
		if (!ctx || ctx->accountId == kInvalidAccountId)
			return nullptr;

		return ctx;
	}

	const UserContext* UserShardState::FindUserContext(UserId userId) const
	{
		const UserContext* ctx = usersById.Find(userId);
		if (!ctx || ctx->accountId == kInvalidAccountId)
			return nullptr;

		return ctx;
	}

	UserContext* UserShardState::FindUserContextByAccount(AccountId accountId)
	{
		if (accountId == kInvalidAccountId)
			return nullptr;

		const auto it = accountUsers.find(accountId);
		if (it == accountUsers.end())
			return nullptr;

		UserContext* ctx = FindUserContext(it->second);
		if (!ctx || ctx->accountId != accountId)
		{
			accountUsers.erase(it);
			return nullptr;
		}

		return ctx;
	}

	const UserContext* UserShardState::FindUserContextByAccount(AccountId accountId) const
	{
		if (accountId == kInvalidAccountId)
			return nullptr;

		const auto it = accountUsers.find(accountId);
		if (it == accountUsers.end())
			return nullptr;

		const UserContext* ctx = FindUserContext(it->second);
		return (ctx && ctx->accountId == accountId) ? ctx : nullptr;
	}

	UserContext* UserShardState::EnsureUserContext(AccountId accountId)
	{
		if (accountId == kInvalidAccountId)
			return nullptr;

		if (auto* existing = FindUserContextByAccount(accountId))
			return existing;

		return AllocUserContext(accountId);
	}

	UserShardState& GetOrCreateUserShardState(ShardLocal& local)
	{
		if (!local.usersState)
		{
			local.usersState = std::make_shared<UserShardState>();
			local.usersState->shardIndex = static_cast<uint16>(local.shardIndex);
		}
		return *local.usersState;
	}

}
