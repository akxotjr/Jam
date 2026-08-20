#include "pch.h"
#include "jamnet/core/executor/ShardDirectory.h"

#include <limits>

namespace jam
{
	namespace
	{
		constexpr uint16 kAnyNumaNode = 0xFFFF;

		bool PickShardAffinitySlot(
			const std::vector<ThreadAffinitySlot>& slots,
			uint32 shardIndex,
			uint16 preferredNode,
			uint32 offset,
			ThreadAffinitySlot& out)
		{
			if (slots.empty())
				return false;

			if (preferredNode != kAnyNumaNode)
			{
				size_t matchCount = 0;
				for (const auto& slot : slots)
				{
					if (slot.numaNode == preferredNode && slot.core.mask != 0)
						++matchCount;
				}

				if (matchCount != 0)
				{
					const size_t targetIndex = static_cast<size_t>(shardIndex) % matchCount;
					size_t currentIndex = 0;
					for (const auto& slot : slots)
					{
						if (slot.numaNode != preferredNode || slot.core.mask == 0)
							continue;

						if (currentIndex == targetIndex)
						{
							out = slot;
							return true;
						}

						++currentIndex;
					}
				}
			}

			out = slots[(static_cast<size_t>(offset) + shardIndex) % slots.size()];
			return out.core.mask != 0;
		}
	}

	ShardDirectory::~ShardDirectory()
	{
		try { StopAll(); JoinAll(); } catch (...) {}
	}

	void ShardDirectory::Init()
	{
		m_shards.reserve(m_config.numShards);
		for (uint32 i = 0; i < m_config.numShards; ++i)
		{
			ShardExecutorConfig c = m_config.shardCfg;
			c.index = static_cast<int32>(i);
			auto shard = std::make_shared<ShardExecutor>(c);
			m_shards.emplace_back(std::move(shard));
		}

		m_routePlacementCounts.assign(m_shards.size(), 0);
	}

	void ShardDirectory::Start()
	{
		AttachSlots();
		AssignCoreSlots();

		for (auto& s : m_shards)
			if (s) s->Start();
	}

	void ShardDirectory::StopAll() const
	{
		for (auto& s : m_shards)
			if (s) s->BeginDrain();
		for (auto& s : m_shards)
			if (s) s->Stop();
	}

	void ShardDirectory::JoinAll() const
	{
		for (auto& s : m_shards)
			if (s) s->Join();
	}

	void ShardDirectory::AttachSlots()
	{
		const uint64 n = static_cast<uint64>(m_shards.size());
		m_slots.resize(n);

		for (uint64 i = 0; i < n; ++i)
		{
			m_slots[i].shardId = static_cast<uint32>(i);
			if (m_shards[i])
				m_shards[i]->AttachSlot(&m_slots[i]);
		}
	}

	void ShardDirectory::AssignCoreSlots()
	{
		if (!m_config.pinShardWorkers)
			return;

		std::vector<ThreadAffinitySlot> slots = m_config.affinitySlots;
		if (slots.empty())
			slots = BuildRoundRobinCoreSlots(QueryNumaNodesWithPrimaryCoreSlots());

		if (slots.empty())
		{
			JAM_LOG_WARN("NUMA/core topology query returned no physical core slots; shard pinning disabled");
			return;
		}

		for (uint32 i = 0; i < m_shards.size(); ++i)
		{
			auto& shard = m_shards[i];
			if (!shard)
				continue;

			const uint32 affinitySlotIndex = RemapExecutorAffinitySlot(i);

			ThreadAffinitySlot slot = {};
			if (!PickShardAffinitySlot(slots, affinitySlotIndex, m_config.shardCfg.numaNode, m_config.affinitySlotOffset, slot))
				continue;

			shard->PinCoreSlot(slot.core, slot.numaNode);
			JAM_LOG_DEBUG("ShardExecutor#{} affinity assigned. numaNode={}, group={}, mask=0x{:X}, physicsSiblingMask=0x{:X}",
				i,
				slot.numaNode,
				slot.core.group,
				static_cast<unsigned long long>(slot.core.mask),
				static_cast<unsigned long long>(slot.core.siblingMask));
		}
	}

	uint64 ShardDirectory::Size() const
	{
		return static_cast<uint64>(m_shards.size());
	}

	uint64 ShardDirectory::PickShard(RouteKey key) const
	{
		const uint64 n = Size();
		if (!IsValidRouteKey(key) || n == 0) return 0;
		return m_routing.PickStableShard(key, static_cast<uint32>(n));
	}

	RouteAssignment ShardDirectory::PlaceRoute(RouteKey key, const RoutePlacementOptions& opt) const
	{
		const uint64 n = Size();
		if (n == 0)
			return {};

		std::scoped_lock guard(m_routePlacementMutex);
		if (m_routePlacementCounts.size() != n)
			m_routePlacementCounts.assign(static_cast<size_t>(n), 0);

		const uint32 shardCount = static_cast<uint32>(n);
		std::vector<RouteLoadInfo> loads;
		loads.reserve(shardCount);
		for (uint32 shardIndex = 0; shardIndex < shardCount; ++shardIndex)
		{
			const auto shard = ShardAt(shardIndex);

			const uint64 placementLoad = (shardIndex < m_routePlacementCounts.size()) ? m_routePlacementCounts[shardIndex] : 0;
			const uint64 load		   = shard ? shard->GetQueueSize() + placementLoad * 1024ull : std::numeric_limits<uint64>::max();
			loads.push_back(RouteLoadInfo{ .shardIndex = shardIndex, .load = load });
		}

		RouteAssignment assignment = m_routing.PlaceRoute(key, loads, opt);

		if (IsValidRouteAssignment(assignment) && assignment.shardIndex < m_routePlacementCounts.size())
			++m_routePlacementCounts[assignment.shardIndex];

		return assignment;
	}

	void ShardDirectory::ReleaseRoute(const RouteAssignment& assignment) const
	{
		if (!IsValidRouteAssignment(assignment))
			return;

		std::scoped_lock guard(m_routePlacementMutex);
		if (assignment.shardIndex < m_routePlacementCounts.size() && m_routePlacementCounts[assignment.shardIndex] != 0)
			--m_routePlacementCounts[assignment.shardIndex];
	}

	void ShardDirectory::ReleaseRoute(uint16 shardIndex) const
	{
		std::scoped_lock guard(m_routePlacementMutex);
		if (shardIndex < m_routePlacementCounts.size() && m_routePlacementCounts[shardIndex] != 0)
			--m_routePlacementCounts[shardIndex];
	}

	std::shared_ptr<ShardExecutor> ShardDirectory::ShardAt(uint64 index) const
	{
		if (index >= Size()) return {};
		return m_shards[index];
	}
}
