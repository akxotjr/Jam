#include "pch.h"
#include "jamnet/core/executor/ShardExecutor.h"
#include "jamnet/runtime/world/WorldShardState.h"
#include "jamnet/runtime/world/WorldBase.h"

namespace jam::net
{
	LocalWorldId WorldShardState::AllocLocalWorldId()
	{
		return static_cast<LocalWorldId>(worldsById.AllocId(shardIndex));
	}

	void WorldShardState::FreeLocalWorldId(LocalWorldId localWorldId)
	{
		if (const auto* object = worldsById.FindAny(localWorldId))
		{
			UnregisterWorld(object->GetWorldKey());
		}
		else
		{
			auto it = std::ranges::find_if(authoritativeWorldsByKey, [localWorldId](const auto& pair)
				{
					return pair.second.localWorldId == localWorldId;
				});
			if (it != authoritativeWorldsByKey.end())
				UnregisterWorld(it->first);
		}
		worldsById.FreeId(localWorldId);
	}

	bool WorldShardState::AdoptWorld(std::unique_ptr<WorldBase> world)
	{
		return worldsById.Adopt(std::move(world));
	}

	bool WorldShardState::BeginDestroyWorld(LocalWorldId localWorldId, eMailboxCloseMode mode)
	{
		if (!FindWorld(localWorldId))
			return false;

		return worldsById.BeginDestroy(localWorldId, mode, [this](RuntimeId id)
			{
				FreeLocalWorldId(static_cast<LocalWorldId>(id));
			});
	}

	WorldBase* WorldShardState::FindWorld(LocalWorldId localWorldId)
	{
		return worldsById.Find(localWorldId);
	}

	const WorldBase* WorldShardState::FindWorld(LocalWorldId localWorldId) const
	{
		return worldsById.Find(localWorldId);
	}

	ShardOwnedObjectRefSlot<WorldBase> WorldShardState::FindWorldRef(LocalWorldId localWorldId)
	{
		return worldsById.FindRef(localWorldId);
	}

	ShardOwnedObjectRefSlot<const WorldBase> WorldShardState::FindWorldRef(LocalWorldId localWorldId) const
	{
		return worldsById.FindRef(localWorldId);
	}

	WorldBase* WorldShardState::FindWorld(const WorldKey& key)
	{
		const LocalWorldId localWorldId = FindLocalWorldId(key);
		return localWorldId != kInvalidLocalWorldId ? FindWorld(localWorldId) : nullptr;
	}

	const WorldBase* WorldShardState::FindWorld(const WorldKey& key) const
	{
		const LocalWorldId localWorldId = FindLocalWorldId(key);
		return localWorldId != kInvalidLocalWorldId ? FindWorld(localWorldId) : nullptr;
	}

	ShardOwnedObjectRefSlot<WorldBase> WorldShardState::FindWorldRef(const WorldKey& key)
	{
		const LocalWorldId localWorldId = FindLocalWorldId(key);
		return localWorldId != kInvalidLocalWorldId ? FindWorldRef(localWorldId) : ShardOwnedObjectRefSlot<WorldBase>{};
	}

	ShardOwnedObjectRefSlot<const WorldBase> WorldShardState::FindWorldRef(const WorldKey& key) const
	{
		const LocalWorldId localWorldId = FindLocalWorldId(key);
		return localWorldId != kInvalidLocalWorldId ? FindWorldRef(localWorldId) : ShardOwnedObjectRefSlot<const WorldBase>{};
	}

	LocalWorldId WorldShardState::FindLocalWorldId(const WorldKey& key) const
	{
		auto it = localIdsByKey.find(key);
		return (it != localIdsByKey.end()) ? it->second : kInvalidLocalWorldId;
	}

	void WorldShardState::RegisterWorld(const WorldConfig& config, LocalWorldId localWorldId)
	{
		if (!config.key.IsIssued() || localWorldId == kInvalidLocalWorldId)
			return;

		UnregisterWorld(config.key);

		WorldMeta entry{};
		entry.key			= config.key;
		entry.localWorldId	= localWorldId;
		entry.kind			= config.desc.kind;
		entry.group			= config.desc.group;
		entry.capacity		= config.desc.capacity;
		entry.runtime		= (config.desc.kind == eWorldKind::Physical) ? ePhysicalWorldRuntimeState::Standby : ePhysicalWorldRuntimeState::Active;
		
		localIdsByKey[config.key] = localWorldId;
		authoritativeWorldsByKey[config.key] = entry;
		worldsByDesc.emplace(config.key.descId, config.key);
		if (config.desc.group != kInvalidWorldGroup)
			worldsByGroup.emplace(config.desc.group, config.key);
	}

	void WorldShardState::UnregisterWorld(const WorldKey& key)
	{
		if (!key.IsIssued())
			return;

		localIdsByKey.erase(key);
		authoritativeWorldsByKey.erase(key);
		std::erase_if(worldsByDesc, [&key](const auto& pair)
			{
				return pair.second == key;
			});
		std::erase_if(worldsByGroup, [&key](const auto& pair)
			{
				return pair.second == key;
			});
	}

	WorldMeta* WorldShardState::FindAuthoritativeWorldEntry(const WorldKey& key)
	{
		auto it = authoritativeWorldsByKey.find(key);
		return (it != authoritativeWorldsByKey.end()) ? &it->second : nullptr;
	}

	const WorldMeta* WorldShardState::FindAuthoritativeWorldEntry(const WorldKey& key) const
	{
		auto it = authoritativeWorldsByKey.find(key);
		return (it != authoritativeWorldsByKey.end()) ? &it->second : nullptr;
	}

	bool WorldShardState::TryReserveMemberSlot(const WorldKey& key)
	{
		WorldMeta* entry = FindAuthoritativeWorldEntry(key);
		if (!entry || !entry->HasCapacity())
			return false;

		++entry->memberCount;
		RefreshRuntimeState(key);
		return true;
	}

	void WorldShardState::ReleaseMemberSlot(const WorldKey& key)
	{
		WorldMeta* entry = FindAuthoritativeWorldEntry(key);
		if (!entry || entry->memberCount == 0)
			return;

		--entry->memberCount;
		entry->activePresenceCount = std::min(entry->activePresenceCount, entry->memberCount);
		RefreshRuntimeState(key);
	}

	void WorldShardState::PromoteMemberPresence(const WorldKey& key)
	{
		WorldMeta* entry = FindAuthoritativeWorldEntry(key);
		if (!entry || entry->kind != eWorldKind::Physical || entry->runtime == ePhysicalWorldRuntimeState::Closing)
			return;

		if (entry->activePresenceCount < entry->memberCount)
			++entry->activePresenceCount;
		RefreshRuntimeState(key);
	}

	void WorldShardState::DemoteMemberPresence(const WorldKey& key)
	{
		WorldMeta* entry = FindAuthoritativeWorldEntry(key);
		if (!entry || entry->activePresenceCount == 0)
			return;

		--entry->activePresenceCount;
		RefreshRuntimeState(key);
	}

	void WorldShardState::RefreshRuntimeState(const WorldKey& key)
	{
		WorldMeta* entry = FindAuthoritativeWorldEntry(key);
		if (!entry || entry->kind != eWorldKind::Physical || entry->runtime == ePhysicalWorldRuntimeState::Closing)
			return;

		if (entry->activePresenceCount > 0)
		{
			entry->runtime = ePhysicalWorldRuntimeState::Active;
			return;
		}

		if (entry->runtime == ePhysicalWorldRuntimeState::Active || entry->runtime == ePhysicalWorldRuntimeState::Paused)
		{
			entry->runtime = ePhysicalWorldRuntimeState::Paused;
			return;
		}

		entry->runtime = ePhysicalWorldRuntimeState::Standby;
	}

	WorldShardState& GetOrCreateWorldShardState(jam::ShardLocal& local)
	{
		if (!local.worldState)
		{
			local.worldState = std::make_shared<WorldShardState>();
			local.worldState->shardIndex = local.shardIndex;
		}

		return *local.worldState;
	}


}
