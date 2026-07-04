#include "pch.h"
#include "jamnet/runtime/world/core/WorldDirectory.h"


namespace jam::net
{
	namespace
	{
		void RemoveIndexedWorld(WorldDirectorySnapshot& snapshot, const WorldKey& key)
		{
			snapshot.worldsByKey.erase(key);
			std::erase_if(snapshot.worldsByArchetypeKey, [&key](const auto& pair)
				{
					return pair.second == key;
				});
			std::erase_if(snapshot.worldsByGroup, [&key](const auto& pair)
				{
					return pair.second == key;
				});
		}
	}



	void WorldDirectory::PublishWorld(const WorldMeta& entry)
	{
		if (!entry.IsValid())
			return;

		m_snapshot.Update([&](WorldDirectorySnapshot& next)
			{
				RemoveIndexedWorld(next, entry.key);
				next.worldsByKey[entry.key] = entry;
				next.worldsByArchetypeKey.emplace(entry.key.archetypeKey, entry.key);
				if (entry.group != kInvalidWorldGroup)
					next.worldsByGroup.emplace(entry.group, entry.key);
			});
	}

	void WorldDirectory::RemoveWorld(const WorldKey& key)
	{
		if (!key.IsIssued())
			return;

		m_snapshot.Update([&](WorldDirectorySnapshot& next)
			{
				RemoveIndexedWorld(next, key);
			});
	}

	void WorldDirectory::UpdateMemberCount(const WorldKey& key, uint32 memberCount)
	{
		if (!key.IsIssued())
			return;

		m_snapshot.Update([&](WorldDirectorySnapshot& next)
			{
				auto it = next.worldsByKey.find(key);
				if (it != next.worldsByKey.end())
				{
					it->second.memberCount = memberCount;
				}
			});
	}

	void WorldDirectory::PublishUserMemberships(const UserMembershipSnapshotEntry& entry)
	{
		if (entry.userId == kInvalidUserId)
			return;

		m_snapshot.Update([&](WorldDirectorySnapshot& next)
			{
				next.usersById[entry.userId] = entry;
			});
	}

	void WorldDirectory::RemoveUserMemberships(UserId userId)
	{
		if (userId == kInvalidUserId)
			return;

		m_snapshot.Update([&](WorldDirectorySnapshot& next)
			{
				next.usersById.erase(userId);
			});
	}

	std::optional<WorldMeta> WorldDirectory::FindWorld(const WorldKey& key) const
	{
		if (!key.IsIssued())
			return std::nullopt;

		auto snapshot = m_snapshot.Load();
		if (!snapshot)
			return std::nullopt;

		auto it = snapshot->worldsByKey.find(key);
		return (it != snapshot->worldsByKey.end()) ? std::optional(it->second) : std::nullopt;
	}

	std::vector<WorldMeta> WorldDirectory::FindWorldsByArchetype(WorldArchetypeKey archetypeKey) const
	{
		if (!IsValidAssetKey(archetypeKey))
			return {};

		auto snapshot = m_snapshot.Load();
		if (!snapshot)
			return {};

		std::vector<WorldMeta> worlds;
		const auto [first, last] = snapshot->worldsByArchetypeKey.equal_range(archetypeKey);
		for (auto it = first; it != last; ++it)
		{
			auto worldIt = snapshot->worldsByKey.find(it->second);
			if (worldIt != snapshot->worldsByKey.end())
				worlds.push_back(worldIt->second);
		}

		return worlds;
	}

	std::vector<WorldMeta> WorldDirectory::FindWorldsByGroup(WorldGroup group) const
	{
		if (group == kInvalidWorldGroup)
			return {};

		auto snapshot = m_snapshot.Load();
		if (!snapshot)
			return {};

		std::vector<WorldMeta> worlds;
		const auto [first, last] = snapshot->worldsByGroup.equal_range(group);
		for (auto it = first; it != last; ++it)
		{
			auto worldIt = snapshot->worldsByKey.find(it->second);
			if (worldIt != snapshot->worldsByKey.end())
				worlds.push_back(worldIt->second);
		}

		return worlds;
	}

	std::optional<UserMembershipSnapshotEntry> WorldDirectory::FindUserMembershipEntry(UserId userId) const
	{
		if (userId == kInvalidUserId)
			return std::nullopt;

		auto snapshot = m_snapshot.Load();
		if (!snapshot)
			return std::nullopt;

		auto it = snapshot->usersById.find(userId);
		return (it != snapshot->usersById.end()) ? std::optional(it->second) : std::nullopt;
	}
}
