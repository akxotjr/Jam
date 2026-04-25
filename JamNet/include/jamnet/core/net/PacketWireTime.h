#pragma once

#include "jamnet/core/net/Buffer.h"
#include "jamnet/core/net/PacketStructure.h"
#include "jamnet/core/utils/Clock.h"

namespace jam::net
{
	uint64 CaptureWireTimestampNow();

	// Patch PING/PONG wire timestamps on a fully built UDP datagram chain right before send.
	void PatchOutgoingSystemWireTime(PacketChain& chain, uint64 wireNow_ns);
}
