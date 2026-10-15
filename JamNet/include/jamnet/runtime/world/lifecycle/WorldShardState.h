#pragma once

#include "jamnet/core/executor/ShardOwnedObject.h"
#include "jamnet/runtime/session/UserContext.h"
#include "jamnet/runtime/world/data/WorldInstanceDatabase.h"
#include "jamnet/runtime/world/lifecycle/WorldActionTypes.h"
#include "jamnet/runtime/world/lifecycle/WorldTransitionTypes.h"

#include <limits>
#include <functional>
#include <memory>
#include <unordered_map>


namespace jam
{
	struct ShardLocal;
}

namespace jam::net
{
	class WorldBase;
	struct WorldUserContext;

	inline constexpr uint32 kInvalidWorldShardIndex = std::numeric_limits<uint32>::max();

	// Flat logical-instance and live world record. World-only fields are meaningful
	// while world.IsValid(); state remains authoritative across world absence.
	struct WorldRecord
	{
		WorldInstanceRef			instance			= {};
		WorldRef				world				= {};
		eWorldInstanceStartup		startup				= eWorldInstanceStartup::OnDemand;
		eWorldInstanceLifecycle		lifecycle			= eWorldInstanceLifecycle::Persistent;
		WorldGroup					group				= kInvalidWorldGroup;
		uint32						capacity			= 0;
		uint32						memberCount			= 0;
		// Detached source slots remain available for recovery until final leave.
		uint32						recoveryMemberCount = 0;
		eWorldRuntimeState			state				= eWorldRuntimeState::Absent;
		uint32						lifecyclePinCount	= 0;

		bool IsValid() const { return instance.IsValid(); }
		bool HasWorld() const { return world.IsValid(); }
		bool HasMembershipCapacity() const { return capacity == 0 || static_cast<uint64>(memberCount) + recoveryMemberCount < capacity; }
	};

	struct WorldShardState
	{
		uint16																shardIndex = 0;
		ShardOwnedObjectTable<WorldBase>									worldsById;
		std::unordered_map<WorldId, WorldRecord>							worldRecordsById;

		WorldId										AllocWorldId();
		void										FreeWorldId(WorldId worldId);

		bool										AdoptWorld(std::unique_ptr<WorldBase> world);
		bool										BeginDestroyWorld(WorldId worldId, eMailboxCloseMode mode = eMailboxCloseMode::Drain, std::function<void()> onDestroyed = nullptr);

		WorldBase*									FindWorld(WorldId worldId);
		const WorldBase*							FindWorld(WorldId worldId) const;
		ShardOwnedObjectRefSlot<WorldBase>			FindWorldRef(WorldId worldId);
		ShardOwnedObjectRefSlot<const WorldBase>	FindWorldRef(WorldId worldId) const;

		void										RegisterWorld(const WorldConfig& config);
		void										UnregisterWorld(WorldId worldId);
		WorldRecord*								FindAuthoritativeWorldEntry(WorldId worldId);
		const WorldRecord*							FindAuthoritativeWorldEntry(WorldId worldId) const;

		// Record counters and lifetime only; ServerWorld owns all content work.
		eWorldTransitionFailure JoinWorldMembership(const WorldRef& world, WorldUserContext user, WorldTransitionToken token, std::string entryPoint);
		bool DetachWorldMembership(const WorldRef& world, UserId userId, WorldTransitionToken token);
		bool CancelWorldMembership(const WorldRef& world, UserId userId, WorldTransitionToken token);
		bool RecoverWorldMembership(const WorldRef& world, WorldUserContext user, WorldTransitionToken token);
		bool CompleteWorldMembershipLeave(const WorldRef& world, UserId userId, WorldTransitionToken token);
		bool CompleteWorldMembershipEntry(const WorldRef& world, UserId userId, WorldTransitionToken token);
		bool BeginDestroyMembershipWorld(WorldId worldId, eMailboxCloseMode mode, std::function<void()> onDestroyed);
	};

	WorldShardState& GetOrCreateWorldShardState(ShardLocal& local);

}
