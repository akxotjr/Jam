#include "pch.h"
#include "jamnet/runtime/protocol/codec/RuntimePacketCodec.h"

#include "jamnet/core/executor/ShardExecutor.h"

namespace jam::net
{
	flatbuffers::FlatBufferBuilder& ResetRuntimePacketBuilder(ShardLocal& local)
	{
		if (!local.packetScratch)
			local.packetScratch = std::make_shared<RuntimePacketScratch>();
		local.packetScratch->builder.Clear();
		return local.packetScratch->builder;
	}
}
