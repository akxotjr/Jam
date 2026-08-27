#pragma once

#include "jamnet/runtime/session/UserContext.h"
#include "jamnet/runtime/world/lifecycle/WorldTransitionTypes.h"

#include <jampx/PhysicsTypes.h>

#include <functional>
#include <span>
#include <string>

namespace jam::net
{
	class ServerWorld;
	struct WorldUserContext;

	struct ServerWorldMemberContentContext
	{
		AccountId				accountId = kInvalidAccountId;
		UserId					userId = kInvalidUserId;
		WorldTransitionToken	transitionToken{};
		WorldEventCorrelation	correlation{};
		std::string				entryPoint;
	};

	class IWorldContent
	{
	public:
		using PrepareMemberCompletion = std::function<void(bool)>;

		virtual ~IWorldContent() = default;

		virtual bool Initialize(ServerWorld& world)
		{
			(void)world;
			return true;
		}

		virtual void Close(ServerWorld& world)
		{
			(void)world;
		}

		virtual void OnPhysicsEvents(ServerWorld& world, std::span<const px::PhysicsEvent> events)
		{
			(void)world;
			(void)events;
		}

		virtual void PrepareMemberContent(ServerWorld& world, const ServerWorldMemberContentContext& context, PrepareMemberCompletion completion)
		{
			(void)world;
			(void)context;
			if (completion) completion(true);
		}

		virtual void RollbackMemberContent(ServerWorld& world, UserId userId, WorldTransitionToken transitionToken)
		{
			(void)world;
			(void)userId;
			(void)transitionToken;
		}

		virtual bool CommitMemberLeave(ServerWorld& world, UserId userId, WorldTransitionToken transitionToken)
		{
			(void)world;
			(void)userId;
			(void)transitionToken;
			return true;
		}

		virtual bool RestoreMemberContent(ServerWorld& world, const WorldUserContext& user, WorldTransitionToken transitionToken)
		{
			(void)world;
			(void)user;
			(void)transitionToken;
			return true;
		}
	};
}
