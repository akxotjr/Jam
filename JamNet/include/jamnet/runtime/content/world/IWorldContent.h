#pragma once

#include "jamnet/runtime/session/UserContext.h"
#include "jamnet/runtime/world/lifecycle/WorldTransitionTypes.h"

#include <jampx/PhysicsTypes.h>

#include <span>
#include <string>

namespace jam::net
{
	class ServerWorld;
	struct WorldUserContext;

	// Built by ServerWorld for admission, preparation, and later activation.
	struct WorldMembershipContext
	{
		AccountId accountId = kInvalidAccountId;
		UserId userId = kInvalidUserId;
		WorldTransitionToken transitionToken{};
		WorldEventCorrelation correlation{};
		std::string entryPoint;
	};

	// Provides game-specific World behavior and work associated with membership transitions.
	// Membership registration and transition state remain owned by the runtime.
	// Membership hooks finish synchronously on the owning World shard.
	class IWorldContent
	{
	public:
		virtual ~IWorldContent() = default;

		// World lifecycle hooks.
		virtual bool OnInitialize(ServerWorld& world) { (void)world; return true; }
		virtual void OnClose(ServerWorld& world) { (void)world; }
		// Admission check before AddMember. Must not mutate game state.
		virtual bool OnJoiningMembership(ServerWorld& world, const WorldMembershipContext& context)
		{
			(void)world; (void)context;
			return true;
		}
		// Validate and retain entry data. Do not create a Player or start replication here.
		virtual bool OnPreparedMembership(ServerWorld& world, const WorldMembershipContext& context)
		{
			(void)world; (void)context;
			return true;
		}
		// Client has applied the committed World. Create/activate its game state.
		// Return success on the World shard, after any failed activation cleanup.
		virtual bool OnActivatedMembership(ServerWorld& world, const WorldMembershipContext& context)
		{
			(void)world; (void)context;
			return true;
		}
		// Capture rollback state before local membership is removed.
		virtual bool OnDetachingMembership(ServerWorld& world, UserId userId, WorldTransitionToken token)
		{
			(void)world; (void)userId; (void)token;
			return true;
		}
		// Target entry is cancelled. Called while its Member is still available for cleanup.
		virtual bool OnRollbackMembership(ServerWorld& world, UserId userId, WorldTransitionToken token)
		{
			(void)world; (void)userId; (void)token;
			return true;
		}
		// Server transition committed; finalize the detached source state.
		virtual bool OnRemovedMembership(ServerWorld& world, UserId userId, WorldTransitionToken token)
		{
			(void)world; (void)userId; (void)token;
			return true;
		}
		// Source membership has been reattached. Restore its retained state, not a new Player.
		virtual bool OnRestoredMembership(ServerWorld& world, const WorldUserContext& user, WorldTransitionToken token)
		{
			(void)world; (void)user; (void)token;
			return true;
		}
		// Server accepted the target. Discard target-side rollback bookkeeping.
		virtual void OnCommittedMembership(ServerWorld& world, UserId userId, WorldTransitionToken token)
		{
			(void)world; (void)userId; (void)token;
		}

		virtual void OnPhysicsEvents(ServerWorld& world, std::span<const px::PhysicsEvent> events)
		{
			(void)world;
			(void)events;
		}

	};
}
