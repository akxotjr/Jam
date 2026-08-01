#pragma once

#include "jamnet/core/executor/AtomicSnapshot.h"
#include "jamnet/runtime/world/lifecycle/WorldShardState.h"

#include <optional>
#include <unordered_map>

namespace jam::net
{
	// Read-optimized published view copied from shard-authoritative world state.
	struct WorldDirectorySnapshot
	{
		std::unordered_map<WorldInstanceId, WorldRecord, WorldInstanceIdHash>	records;
		std::unordered_map<WorldId, WorldInstanceId>						instanceIdsByRuntimeId;
	};

	class WorldDirectory
	{
	public:
		WorldDirectory() = default;
		~WorldDirectory() = default;

		bool	Publish(const WorldRecord& record);
		bool	Clear(WorldId worldId);

		std::optional<WorldRecord>	FindByInstanceId(WorldInstanceId instanceId) const;
		std::optional<WorldRecord>	FindByWorldId(WorldId worldId) const;

	private:
		AtomicSnapshot<WorldDirectorySnapshot>	m_snapshot;
	};
}
