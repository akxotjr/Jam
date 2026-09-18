#include "pch.h"
#include "jamnet/core/net/PacketWireTime.h"

namespace jam::net
{
	uint64 CaptureWireTimestampNow()
	{
		return NOW_NS();
	}

	void ApplyWireTimePatches(const std::span<const WireTimePatch> patches, const uint64 wireNow_ns)
	{
		for (const WireTimePatch& patch : patches)
		{
			if (patch.destination)
				std::memcpy(patch.destination, &wireNow_ns, sizeof(wireNow_ns));
		}
	}
}
