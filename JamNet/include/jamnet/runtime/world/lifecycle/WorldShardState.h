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

	// Flat logical-instance and runtime record. Runtime-only fields are meaningful
	// while runtime.IsValid(); state remains authoritative across runtime absence.
	struct WorldRecord
	{
		WorldInstanceRef			instance			= {};
		WorldRuntimeRef				runtime				= {};
		eWorldInstanceStartup		startup				= eWorldInstanceStartup::OnDemand;
		eWorldInstanceLifecycle		lifecycle			= eWorldInstanceLifecycle::Persistent;
		WorldGroup					group				= kInvalidWorldGroup;
		uint32						capacity			= 0;
		uint32						memberCount			= 0;
		uint32						activePresenceCount = 0;
		eWorldRuntimeState			state				= eWorldRuntimeState::Absent;
		uint32						pendingAttachCount	= 0;
		uint32						lifecyclePinCount	= 0;
		uint64						destroyRevision		= 0;

		bool IsValid() const { return instance.IsValid(); }
		bool HasRuntime() const { return runtime.IsValid(); }
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
		std::unordered_map<WorldId, WorldRecord>							authoritativeWorldsByRuntimeId;
		std::unordered_multimap<WorldArchetypeKey, WorldId>				worldsByArchetypeKey;
		std::unordered_multimap<WorldGroup, WorldId>						worldsByGroup;
		std::unordered_map<WorldTransitionToken, WorldTransitionMember, WorldTransitionTokenHash> transitionMembers;

		WorldId										AllocWorldId();
		void										FreeWorldId(WorldId runtimeId);

		bool										AdoptWorld(std::unique_ptr<WorldBase> world);
		bool										BeginDestroyWorld(WorldId runtimeId, eMailboxCloseMode mode = eMailboxCloseMode::Drain,
			std::function<void()> onDestroyed = nullptr);

		WorldBase*									FindWorld(WorldId runtimeId);
		const WorldBase*							FindWorld(WorldId runtimeId) const;
		ShardOwnedObjectRefSlot<WorldBase>			FindWorldRef(WorldId runtimeId);
		ShardOwnedObjectRefSlot<const WorldBase>	FindWorldRef(WorldId runtimeId) const;

		void						RegisterWorld(const WorldConfig& config);
		void						UnregisterWorld(WorldId runtimeId);
		WorldRecord*				FindAuthoritativeWorldEntry(WorldId runtimeId);
		const WorldRecord*			FindAuthoritativeWorldEntry(WorldId runtimeId) const;
		bool						TryReserveMemberSlot(WorldId runtimeId);
		void						ReleaseMemberSlot(WorldId runtimeId);
		void						PromoteMemberPresence(WorldId runtimeId);
		void						DemoteMemberPresence(WorldId runtimeId);
		void						RefreshRuntimeState(WorldId runtimeId);

		bool						ReserveEnter(WorldTransitionToken token, uint64 userId, const WorldRuntimeRef& target);
		bool						PrepareEnter(WorldTransitionToken token);
		bool						AttachPrepared(WorldTransitionToken token, const WorldUserContext& user);
		bool						ActivateAttached(WorldTransitionToken token);
		bool						RollbackEnter(WorldTransitionToken token);
		bool						PrepareLeave(WorldTransitionToken token, uint64 userId, const WorldRuntimeRef& source);
		bool						DetachMain(WorldTransitionToken token);
		bool						CancelPreparedLeave(WorldTransitionToken token);
		bool						CommitLeave(WorldTransitionToken token);
		bool						RestoreDetachedMain(WorldTransitionToken token, const WorldUserContext& user);
		bool						DisconnectMember(uint64 userId, const WorldRuntimeRef& runtime);
		bool						TryBeginRuntimeDrain(WorldId runtimeId, uint64 expectedDestroyRevision);
		bool						CanDestroyRuntime(WorldId runtimeId, uint64 expectedDestroyRevision) const;
	};

	WorldShardState& GetOrCreateWorldShardState(ShardLocal& local);

}
