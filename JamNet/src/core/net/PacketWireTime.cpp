#include "pch.h"
#include "jamnet/core/net/PacketWireTime.h"

namespace jam::net
{

	void ApplyWireTimePatches(const std::span<const WireTimePatch> patches, const Timestamp wireNow)
	{
		for (const WireTimePatch& patch : patches)
		{
			if (patch.destination)
				std::memcpy(patch.destination, &wireNow, sizeof(wireNow));
		}
	}
}
