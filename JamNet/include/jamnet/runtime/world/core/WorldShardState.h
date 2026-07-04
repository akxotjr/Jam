#pragma once

#include "jamnet/core/executor/ShardOwnedObject.h"
#include "jamnet/runtime/world/types/WorldActionTypes.h"

#include <limits>
#include <memory>
#include <unordered_map>


namespace jam
{
	struct ShardLocal;
}

namespace jam::net
{
	class WorldBase;

	inline constexpr uint32 kInvalidWorldShardIndex = std::numeric_limits<uint32>::max();

	// Shared world metadata payload. This is authored on the owning shard and
	// copied into the published WorldDirectory snapshot for cross-shard reads.
	struct WorldMeta
	{
		WorldKey					key					= {};
		LocalWorldId				localWorldId		= kInvalidLocalWorldId;
		eWorldKind					kind				= eWorldKind::Physical;
		WorldGroup					group				= kInvalidWorldGroup;
		uint32						capacity			= 0;
		uint32						memberCount			= 0;
		uint32						activePresenceCount = 0;
		ePhysicalWorldRuntimeState	runtime				= ePhysicalWorldRuntimeState::Standby;

		bool IsValid() const { return key.IsIssued(); }
		bool HasCapacity() const { return capacity == 0 || memberCount < capacity; }
	};

	struct WorldShardState
	{
		uint16																shardIndex = 0;
		ShardOwnedObjectTable<WorldBase>									worldsById;
		std::unordered_map<WorldKey, WorldMeta, WorldKeyHash>				authoritativeWorldsByKey;
		std::unordered_map<WorldKey, LocalWorldId, WorldKeyHash>			localIdsByKey;
		std::unordered_multimap<WorldArchetypeKey, WorldKey>				worldsByArchetypeKey;
		std::unordered_multimap<WorldGroup, WorldKey>						worldsByGroup;

		LocalWorldId				AllocLocalWorldId();
		void						FreeLocalWorldId(LocalWorldId localWorldId);

		bool						AdoptWorld(std::unique_ptr<WorldBase> world);
		bool						BeginDestroyWorld(LocalWorldId localWorldId, eMailboxCloseMode mode = eMailboxCloseMode::Drain);

		WorldBase*									FindWorld(LocalWorldId localWorldId);
		const WorldBase*							FindWorld(LocalWorldId localWorldId) const;
		ShardOwnedObjectRefSlot<WorldBase>			FindWorldRef(LocalWorldId localWorldId);
		ShardOwnedObjectRefSlot<const WorldBase>	FindWorldRef(LocalWorldId localWorldId) const;
		WorldBase*									FindWorld(const WorldKey& key);
		const WorldBase*							FindWorld(const WorldKey& key) const;
		ShardOwnedObjectRefSlot<WorldBase>			FindWorldRef(const WorldKey& key);
		ShardOwnedObjectRefSlot<const WorldBase>	FindWorldRef(const WorldKey& key) const;
		LocalWorldId								FindLocalWorldId(const WorldKey& key) const;

		void						RegisterWorld(const WorldConfig& config, LocalWorldId localWorldId);
		void						UnregisterWorld(const WorldKey& key);
		WorldMeta*					FindAuthoritativeWorldEntry(const WorldKey& key);
		const WorldMeta*			FindAuthoritativeWorldEntry(const WorldKey& key) const;
		bool						TryReserveMemberSlot(const WorldKey& key);
		void						ReleaseMemberSlot(const WorldKey& key);
		void						PromoteMemberPresence(const WorldKey& key);
		void						DemoteMemberPresence(const WorldKey& key);
		void						RefreshRuntimeState(const WorldKey& key);
	};

	WorldShardState& GetOrCreateWorldShardState(ShardLocal& local);

}
