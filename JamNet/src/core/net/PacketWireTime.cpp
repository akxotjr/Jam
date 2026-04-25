#include "pch.h"
#include "jamnet/core/net/PacketWireTime.h"
#include "jamnet/core/net/PacketBuilder.h"

namespace jam::net
{
	namespace
	{
		bool CopyChainBytes(const PacketChain& chain, uint32 streamOffset, void* dst, uint32 bytes)
		{
			if (bytes == 0)
				return true;

			BYTE* out = reinterpret_cast<BYTE*>(dst);
			uint32 cursor = 0;
			uint32 written = 0;

			for (const BufferSlice& part : chain.Parts())
			{
				if (!part.IsValid() || part.Size() == 0)
					continue;

				const uint32 partSize = part.Size();
				if (streamOffset >= cursor + partSize)
				{
					cursor += partSize;
					continue;
				}

				const uint32 localOffset = (streamOffset > cursor) ? (streamOffset - cursor) : 0;
				const uint32 chunk = std::min<uint32>(partSize - localOffset, bytes - written);
				std::memcpy(out + written, part.Head() + localOffset, chunk);
				written += chunk;
				streamOffset += chunk;
				cursor += partSize;

				if (written == bytes)
					return true;
			}

			return false;
		}

		bool WriteChainBytes(PacketChain& chain, uint32 streamOffset, const void* src, uint32 bytes)
		{
			if (bytes == 0)
				return true;

			const BYTE* in = reinterpret_cast<const BYTE*>(src);
			uint32 cursor = 0;
			uint32 written = 0;

			for (BufferSlice& part : chain.Parts())
			{
				if (!part.IsValid() || part.Size() == 0)
					continue;

				const uint32 partSize = part.Size();
				if (streamOffset >= cursor + partSize)
				{
					cursor += partSize;
					continue;
				}

				const uint32 localOffset = (streamOffset > cursor) ? (streamOffset - cursor) : 0;
				const uint32 chunk = std::min<uint32>(partSize - localOffset, bytes - written);
				std::memcpy(part.Head() + localOffset, in + written, chunk);
				written		 += chunk;
				streamOffset += chunk;
				cursor		 += partSize;

				if (written == bytes)
					return true;
			}

			return false;
		}

		bool ReadPacketHeaderAt(const PacketChain& chain, const uint32 packetOffset, PacketHeader& outHeader, uint32& outHeaderSize, uint32& outPacketSize)
		{
			BYTE headerBytes[PacketHeader::MAX_WIRE_SIZE] = {};

			if (!CopyChainBytes(chain, packetOffset, headerBytes, PacketHeader::BASE_SIZE))
				return false;

			std::memcpy(&outHeader, headerBytes, PacketHeader::BASE_SIZE);
			if (!outHeader.IsValid())
				return false;

			outHeaderSize = outHeader.GetActualSize();
			if (outHeaderSize > PacketHeader::MAX_WIRE_SIZE)
				return false;

			if (!CopyChainBytes(chain, packetOffset, headerBytes, outHeaderSize))
				return false;

			std::memcpy(&outHeader, headerBytes, outHeaderSize);
			if (!outHeader.IsValid())
				return false;

			outPacketSize = outHeader.GetSize();
			return outPacketSize >= outHeaderSize;
		}
	}

	uint64 CaptureWireTimestampNow()
	{
		return NOW_NS();
	}

	void PatchOutgoingSystemWireTime(PacketChain& chain, const uint64 wireNow_ns)
	{
		const uint32 totalSize = chain.TotalSize();
		uint32 packetOffset = 0;

		while (packetOffset < totalSize)
		{
			PacketHeader header{};
			uint32 headerSize = 0;
			uint32 packetSize = 0;
			if (!ReadPacketHeaderAt(chain, packetOffset, header, headerSize, packetSize))
				return;

			if (packetOffset + packetSize > totalSize)
				return;

			if (U2E(ePacketType, header.GetType()) == ePacketType::SYSTEM)
			{
				const auto sysId = U2E(eSystemPacketId, header.GetId());
				const uint32 payloadOffset = packetOffset + headerSize;

				if (sysId == eSystemPacketId::PING && (packetSize - headerSize) >= sizeof(PING_DATA))
				{
					const uint32 fieldOffset = payloadOffset + static_cast<uint32>(offsetof(PING_DATA, t1Wire_ns));
					WriteChainBytes(chain, fieldOffset, &wireNow_ns, sizeof(wireNow_ns));
				}
				else if (sysId == eSystemPacketId::PONG && (packetSize - headerSize) >= sizeof(PONG_DATA))
				{
					const uint32 fieldOffset = payloadOffset + static_cast<uint32>(offsetof(PONG_DATA, t3Wire_ns));
					WriteChainBytes(chain, fieldOffset, &wireNow_ns, sizeof(wireNow_ns));
				}
			}

			packetOffset += packetSize;
		}
	}
}
