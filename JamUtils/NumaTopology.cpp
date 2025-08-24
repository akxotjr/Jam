#include "pch.h"
#include "NumaTopology.h"


namespace jam::utils::sys
{
	static KAFFINITY LsbOne(KAFFINITY m)
	{
		return m & (~m + 1);
	}

	std::vector<NodeInfo> QueryNumaNodesWithPrimaryCoreSlots()
	{
		std::vector<NodeInfo> nodes;

		ULONG highestNode = 0;
		GetNumaHighestNodeNumber(&highestNode);

		for (USHORT node = 0; node <= highestNode; ++node)
		{
			GROUP_AFFINITY nodeMask = {};
			if (!GetNumaNodeProcessorMaskEx(node, &nodeMask))
				continue;

			DWORD len = 0;
			GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &len);
			if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
				continue;

			auto buf = std::unique_ptr<BYTE[]>(new BYTE[len]);
			if (!GetLogicalProcessorInformationEx(RelationProcessorCore, reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.get()), &len))
				continue;

			NodeInfo info = {};
			info.nodeId = node;

			BYTE* cur = buf.get();
			BYTE* end = buf.get() + len;

			while (cur < end)
			{
				auto ex = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(cur);
				if (ex->Relationship == RelationProcessorCore)
				{
					const auto& pr = ex->Processor;
					for (WORD i = 0; i < pr.GroupCount; ++i)
					{
						const GROUP_AFFINITY& gm = pr.GroupMask[i];
						if (gm.Mask == 0)
							continue;

						if (gm.Group == nodeMask.Group && (gm.Mask & nodeMask.Mask) != 0)
						{
							CoreSlot slot{ gm.Group, LsbOne(gm.Mask) };
							info.cores.push_back(slot);
							break;
						}
					}
					cur += ex->Size;
				}

				if (!info.cores.empty())
					nodes.push_back(std::move(info));
			}
		}
		return nodes;
	}

}