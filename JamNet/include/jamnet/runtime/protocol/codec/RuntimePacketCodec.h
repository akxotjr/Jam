#pragma once

#include <flatbuffers/flatbuffers.h>

namespace jam
{
	struct ShardLocal;
}

namespace jam::net
{
	inline constexpr size_t kRuntimePacketScratchInitialSize = 2048;

	struct RuntimePacketScratch
	{
		flatbuffers::FlatBufferBuilder builder{ kRuntimePacketScratchInitialSize };
	};

	flatbuffers::FlatBufferBuilder& ResetRuntimePacketBuilder(ShardLocal& local);
}
