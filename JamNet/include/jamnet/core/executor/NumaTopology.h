#pragma once


namespace jam
{
	struct CoreSlot
	{
		USHORT		group		= 0;	// Processor group
		KAFFINITY	mask		= 0;	// One logical processor from a physical core.
		KAFFINITY	siblingMask = 0;	// SMT sibling of mask, when available.
	};

	struct NodeInfo
	{
		USHORT					nodeId = 0;		// NUMA Node ID
		std::vector<CoreSlot>	cores;			// Logical core-slot per physical core
	};

	struct ThreadAffinitySlot
	{
		USHORT					numaNode = 0xFFFF;
		CoreSlot				core	 = {};
	};

	std::vector<NodeInfo>			QueryNumaNodesWithPrimaryCoreSlots();
	std::vector<ThreadAffinitySlot> BuildRoundRobinCoreSlots(const std::vector<NodeInfo>& nodes);

	inline bool PinCurrentThreadTo(const CoreSlot& slot)
	{
		if (slot.mask == 0)
			return false;

		GROUP_AFFINITY ga = {};
		ga.Group = slot.group;
		ga.Mask  = slot.mask;
		return SetThreadGroupAffinity(GetCurrentThread(), &ga, nullptr) != 0;
	}

	inline void* NumaAllocOnNode(uint64 bytes, USHORT nodeId)
	{
		return VirtualAllocExNuma(GetCurrentProcess(), nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, nodeId);
	}

	inline void NumaFree(void* p)
	{
		if (p) VirtualFree(p, 0, MEM_RELEASE);
	}

}
