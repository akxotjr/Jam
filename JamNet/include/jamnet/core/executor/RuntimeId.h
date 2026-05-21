#pragma once

#include <jambase/JamTypes.h>

namespace jam
{
	using RuntimeId = uint64;

	inline constexpr RuntimeId kInvalidRuntimeId = 0;

	// 64-bit RuntimeId layout:
	// [ Shard Index (16 bit) | Local Index (16 bit) | Generation (32 bit) ]
	inline constexpr uint64 kRuntimeShardIdxShift		= 48;
	inline constexpr uint64 kRuntimeLocalIdxShift		= 32;
	inline constexpr uint64 kRuntimeShardIdxMask		= 0xFFFF'0000'0000'0000ull;
	inline constexpr uint64 kRuntimeLocalIdxMask		= 0x0000'FFFF'0000'0000ull;
	inline constexpr uint64 kRuntimeGenerationMask		= 0x0000'0000'FFFF'FFFFull;

	inline RuntimeId MakeRuntimeId(uint16 shardIndex, uint16 localIndex, uint32 generation)
	{
		return (static_cast<uint64>(shardIndex & 0xFFFF) << kRuntimeShardIdxShift) |
			   (static_cast<uint64>(localIndex & 0xFFFF) << kRuntimeLocalIdxShift) |
			   static_cast<uint64>(generation);
	}

	inline uint16 GetRuntimeShardIndex(RuntimeId id)
	{
		return static_cast<uint16>((id & kRuntimeShardIdxMask) >> kRuntimeShardIdxShift);
	}

	inline uint16 GetRuntimeLocalIndex(RuntimeId id)
	{
		return static_cast<uint16>((id & kRuntimeLocalIdxMask) >> kRuntimeLocalIdxShift);
	}

	inline uint32 GetRuntimeGeneration(RuntimeId id)
	{
		return static_cast<uint32>(id & kRuntimeGenerationMask);
	}

	using SessionId = RuntimeId;
	inline constexpr SessionId kInvalidSessionId = kInvalidRuntimeId;

	inline SessionId MakeSessionId(uint16 shardIndex, uint16 localIndex, uint32 generation)
	{
		return MakeRuntimeId(shardIndex, localIndex, generation);
	}
}
