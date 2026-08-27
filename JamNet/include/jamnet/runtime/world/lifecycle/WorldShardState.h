#pragma once

#include "jamnet/core/executor/ShardOwnedObject.h"
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
		eWorldRuntimeState			state				= eWorldRuntimeState::Absent;
		uint32						pendingAttachCount	= 0;
		uint32						lifecyclePinCount	= 0;
		uint64						destroyRevision		= 0;

		bool IsValid() const { return instance.IsValid(); }
		bool HasWorld() const { return world.IsValid(); }
		bool HasCapacity() const { return capacity == 0 || memberCount < capacity; }
	};

	enum class eWorldTransitionMemberState : uint8
	{
		Reserved = 0,
		Prepared,
		AttachedPendingCommit,
		Active,
		DetachedPendingCommit,
	};

	struct WorldTransitionMember
	{
		WorldTransitionToken		token = {};
		uint64						userId = 0;
		WorldId						worldId = kInvalidWorldId;
		eWorldTransitionMemberState state = eWorldTransitionMemberState::Reserved;
		bool						hostMemberAttached = false;
	};

	struct WorldShardState
	{
		uint16																shardIndex = 0;
		ShardOwnedObjectTable<WorldBase>									worldsById;
		std::unordered_map<WorldId, WorldRecord>							worldRecordsById;
		std::unordered_map<WorldTransitionToken, WorldTransitionMember, WorldTransitionTokenHash> enterTransitionMembers;
		std::unordered_map<WorldTransitionToken, WorldTransitionMember, WorldTransitionTokenHash> leaveTransitionMembers;

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

		bool										ReserveEnter(WorldTransitionToken token, uint64 userId, const WorldRef& target);
		bool										PrepareEnter(WorldTransitionToken token);
		bool										AttachPrepared(WorldTransitionToken token, const WorldUserContext& user);
		bool										ActivateAttached(WorldTransitionToken token);
		bool										RollbackEnter(WorldTransitionToken token);
		bool										PrepareLeave(WorldTransitionToken token, uint64 userId, const WorldRef& source);
		bool										DetachMain(WorldTransitionToken token);
		bool										CancelPreparedLeave(WorldTransitionToken token);
		bool										CommitLeave(WorldTransitionToken token);
		bool										RestoreDetachedMain(WorldTransitionToken token, const WorldUserContext& user);
		bool										DisconnectMember(uint64 userId, const WorldRef& world);
		bool										TryBeginWorldDrain(WorldId worldId, uint64 expectedDestroyRevision);
		bool										CanDestroyWorld(WorldId worldId, uint64 expectedDestroyRevision) const;
	};

	WorldShardState& GetOrCreateWorldShardState(ShardLocal& local);

}
