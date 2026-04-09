#pragma once

#include "jamnet/runtime/world/WorldAssignmentTypes.h"

#include <unordered_map>

namespace jam::net
{
	class WorldDirectory
	{
	public:
		WorldId			FindWorldId(const WorldKey& key) const;
		WorldId			FindOrAddWorld(const WorldKey& key, const WorldOptions& options);
		WorldKey		FindWorldKey(WorldId worldId) const;
		WorldOptions	FindWorldOptions(WorldId worldId) const;
		void			SetWorldOptions(WorldId worldId, const WorldOptions& options);
		void			RemoveWorld(WorldId worldId);

	private:
		WorldId												m_nextWorldId = 1;

		std::unordered_map<WorldKey, WorldId, WorldKeyHash> m_worldIdsByKey;
		std::unordered_map<WorldId, WorldKey>				m_worldKeysById;
		std::unordered_map<WorldId, WorldOptions>			m_worldOptionsById;
	};
}
