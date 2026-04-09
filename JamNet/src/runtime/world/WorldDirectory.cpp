#include "pch.h"
#include "jamnet/runtime/world/WorldDirectory.h"

namespace jam::net
{
	WorldId WorldDirectory::FindWorldId(const WorldKey& key) const
	{
		if (!key.IsValid())
			return INVALID_WORLD_ID;

		auto it = m_worldIdsByKey.find(key);
		return (it != m_worldIdsByKey.end()) ? it->second : INVALID_WORLD_ID;
	}

	WorldId WorldDirectory::FindOrAddWorld(const WorldKey& key, const WorldOptions& options)
	{
		if (!key.IsValid())
			return INVALID_WORLD_ID;

		if (const WorldId existing = FindWorldId(key); existing != INVALID_WORLD_ID)
		{
			SetWorldOptions(existing, options);
			return existing;
		}

		const WorldId worldId = m_nextWorldId++;
		m_worldIdsByKey.emplace(key, worldId);
		m_worldKeysById.emplace(worldId, key);
		m_worldOptionsById.emplace(worldId, options);

		return worldId;
	}

	WorldKey WorldDirectory::FindWorldKey(WorldId worldId) const
	{
		if (worldId == INVALID_WORLD_ID)
			return INVALID_WORLD_KEY;

		auto it = m_worldKeysById.find(worldId);
		return (it != m_worldKeysById.end()) ? it->second : INVALID_WORLD_KEY;
	}

	WorldOptions WorldDirectory::FindWorldOptions(WorldId worldId) const
	{
		if (worldId == INVALID_WORLD_ID)
			return {};

		auto it = m_worldOptionsById.find(worldId);
		return (it != m_worldOptionsById.end()) ? it->second : WorldOptions{};
	}

	void WorldDirectory::SetWorldOptions(WorldId worldId, const WorldOptions& options)
	{
		if (worldId == INVALID_WORLD_ID)
			return;

		m_worldOptionsById[worldId] = options;
	}

	void WorldDirectory::RemoveWorld(WorldId worldId)
	{
		if (worldId == INVALID_WORLD_ID)
			return;

		if (auto keyIt = m_worldKeysById.find(worldId); keyIt != m_worldKeysById.end())
		{
			m_worldIdsByKey.erase(keyIt->second);
			m_worldKeysById.erase(keyIt);
		}

		m_worldOptionsById.erase(worldId);
	}
}
