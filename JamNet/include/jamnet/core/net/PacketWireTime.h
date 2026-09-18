#pragma once

#include <span>

#include "jamnet/core/net/Buffer.h"
#include "jamnet/core/net/PacketStructure.h"
#include "jamnet/core/utils/Clock.h"

namespace jam::net
{
	struct WireTimePatch
	{
		BYTE* destination = nullptr;
	};

	uint64 CaptureWireTimestampNow();
	void ApplyWireTimePatches(std::span<const WireTimePatch> patches, uint64 wireNow_ns);
}
