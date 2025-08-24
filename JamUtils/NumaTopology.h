#pragma once


namespace jam::utils::sys
{
	struct CoreSlot
	{
		USHORT		group = 0;	// Processor group
		KAFFINITY	mask = 0;	// 
	};

	struct NodeInfo
	{
		USHORT					nodeId = 0;		// NUMA Node ID
		std::vector<CoreSlot>	cores;			// Logical core-slot per physical core
	};

	std::vector<NodeInfo> QueryNumaNodesWithPrimaryCoreSlots();

	inline bool PinCurrentThreadTo(const CoreSlot& slot)
	{
		GROUP_AFFINITY ga = {};
		ga.Group = slot.group;
		ga.Mask = slot.mask;
		return SetThreadGroupAffinity(GetCurrentThread(), &ga, nullptr) != 0;
	}

	inline void* NumaAllocOnNode(uint64 bytes, USHORT nodeId)
	{
		return VirtualAllocExNuma(GetCurrentProcess(), nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, nodeId);
	}

	inline void NumaFree(void* p)
	{
		if (p)
			VirtualFree(p, 0, MEM_RELEASE);
	}

}
