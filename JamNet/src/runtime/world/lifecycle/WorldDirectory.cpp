#include "pch.h"
#include "jamnet/runtime/world/lifecycle/WorldDirectory.h"


namespace jam::net
{
	bool WorldDirectory::Publish(const WorldRecord& record)
	{
		if (!record.IsValid() || !record.HasRuntime() || record.runtime.instance != record.instance)
			return false;

		bool published = false;
		m_snapshot.Update([&](WorldDirectorySnapshot& next)
			{
				auto [it, inserted] = next.records.try_emplace(record.instance.instanceId, record);
				if (!inserted)
				{
					if (it->second.runtime.IsValid())
						next.instanceIdsByRuntimeId.erase(it->second.runtime.worldId);
					it->second = record;
				}
				next.instanceIdsByRuntimeId[record.runtime.worldId] = record.instance.instanceId;
				published = true;
			});
		return published;
	}

	bool WorldDirectory::Clear(WorldId worldId)
	{
		if (worldId == kInvalidWorldId)
			return false;

		bool cleared = false;
		m_snapshot.Update([&](WorldDirectorySnapshot& next)
			{
				auto runtimeIt = next.instanceIdsByRuntimeId.find(worldId);
				if (runtimeIt == next.instanceIdsByRuntimeId.end())
					return;

				cleared = next.records.erase(runtimeIt->second) != 0;
				next.instanceIdsByRuntimeId.erase(runtimeIt);
			});
		return cleared;
	}

	std::optional<WorldRecord> WorldDirectory::FindByInstanceId(WorldInstanceId instanceId) const
	{
		if (!instanceId.IsValid())
			return std::nullopt;

		auto snapshot = m_snapshot.Load();
		if (!snapshot)
			return std::nullopt;

		auto it = snapshot->records.find(instanceId);
		return it != snapshot->records.end() ? std::optional(it->second) : std::nullopt;
	}

	std::optional<WorldRecord> WorldDirectory::FindByWorldId(WorldId worldId) const
	{
		if (worldId == kInvalidWorldId)
			return std::nullopt;

		auto snapshot = m_snapshot.Load();
		if (!snapshot)
			return std::nullopt;

		auto runtimeIt = snapshot->instanceIdsByRuntimeId.find(worldId);
		if (runtimeIt == snapshot->instanceIdsByRuntimeId.end())
			return std::nullopt;

		auto recordIt = snapshot->records.find(runtimeIt->second);
		return recordIt != snapshot->records.end() ? std::optional(recordIt->second) : std::nullopt;
	}
}
