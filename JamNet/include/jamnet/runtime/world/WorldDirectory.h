#pragma once

#include "jamnet/core/executor/AtomicSnapshot.h"
#include "jamnet/runtime/UserContext.h"
#include "jamnet/runtime/world/WorldActionTypes.h"
#include "jamnet/runtime/world/WorldShardState.h"

#include <optional>
#include <unordered_map>
#include <vector>


namespace jam::net
{
	struct UserMembershipSnapshotEntry
	{
		UserId								userId		= kInvalidUserId;
		std::vector<WorldMembership>		memberships;
	};

	// Read-optimized published view copied from shard-authoritative world state.
	struct WorldDirectorySnapshot
	{
		std::unordered_map<WorldKey, WorldMeta, WorldKeyHash>		worldsByKey;
		std::unordered_multimap<uint32, WorldKey>					worldsByDesc;
		std::unordered_multimap<WorldGroup, WorldKey>				worldsByGroup;
		std::unordered_map<UserId, UserMembershipSnapshotEntry>		usersById;
	};

	class WorldDirectory
	{
	public:
		WorldDirectory() = default;
		~WorldDirectory() = default;

		void										PublishWorld(const WorldMeta& entry);
		void										RemoveWorld(const WorldKey& key);
		void										UpdateMemberCount(const WorldKey& key, uint32 memberCount);
		void										PublishUserMemberships(const UserMembershipSnapshotEntry& entry);
		void										RemoveUserMemberships(UserId userId);

		std::optional<WorldMeta>					FindWorld(const WorldKey& key) const;
		std::vector<WorldMeta>						FindWorldsByDesc(uint32 descId) const;
		std::vector<WorldMeta>						FindWorldsByGroup(WorldGroup group) const;
		std::optional<UserMembershipSnapshotEntry>	FindUserMembershipEntry(UserId userId) const;

	private:
		AtomicSnapshot<WorldDirectorySnapshot>		m_snapshot;
	};
}
