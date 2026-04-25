#include "pch.h"
#include "jamnet/core/executor/NumaTopology.h"


namespace jam
{
	static KAFFINITY LsbOne(KAFFINITY m)
	{
		return m & (~m + 1);
	}

	std::vector<NodeInfo> QueryNumaNodesWithPrimaryCoreSlots()
	{
		std::vector<NodeInfo> nodes;

		ULONG highestNode = 0;
		if (!GetNumaHighestNodeNumber(&highestNode))
			return nodes;

		DWORD len = 0;
		GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &len);
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || len == 0)
			return nodes;

		auto buf = std::unique_ptr<BYTE[]>(new BYTE[len]);
		if (!GetLogicalProcessorInformationEx(RelationProcessorCore, reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.get()), &len))
			return nodes;

		for (USHORT node = 0; node <= highestNode; ++node)
		{
			GROUP_AFFINITY nodeMask = {};
			if (!GetNumaNodeProcessorMaskEx(node, &nodeMask))
				continue;

			NodeInfo info = {};
			info.nodeId = node;

			BYTE* cur = buf.get();
			BYTE* end = buf.get() + len;

			while (cur < end)
			{
				auto ex = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(cur);
				if (ex->Size == 0 || cur + ex->Size > end)
					break;

				if (ex->Relationship == RelationProcessorCore)
				{
					const auto& pr = ex->Processor;
					for (WORD i = 0; i < pr.GroupCount; ++i)
					{
						const GROUP_AFFINITY& gm = pr.GroupMask[i];
						if (gm.Mask == 0)
							continue;

						if (gm.Group == nodeMask.Group)
						{
							const KAFFINITY localMask = gm.Mask & nodeMask.Mask;
							if (localMask == 0)
								continue;

							CoreSlot slot{ gm.Group, LsbOne(localMask) };
							info.cores.push_back(slot);
							break;
						}
					}
				}

				cur += ex->Size;
			}

			if (!info.cores.empty())
				nodes.push_back(std::move(info));
		}
		return nodes;
	}

	std::vector<ThreadAffinitySlot> BuildRoundRobinCoreSlots(const std::vector<NodeInfo>& nodes)
	{
		std::vector<ThreadAffinitySlot> slots;

		size_t maxCoreCount = 0;
		for (const auto& node : nodes)
			maxCoreCount = std::max(maxCoreCount, node.cores.size());

		for (size_t coreIndex = 0; coreIndex < maxCoreCount; ++coreIndex)
		{
			for (const auto& node : nodes)
			{
				if (coreIndex >= node.cores.size())
					continue;

				slots.push_back(ThreadAffinitySlot{
					.numaNode = node.nodeId,
					.core = node.cores[coreIndex]
				});
			}
		}

		return slots;
	}

}
