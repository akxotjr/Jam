#include "pch.h"
#include "PacketBuilder.h"


namespace jam::net
{
	Sptr<SendBuffer> PacketBuilder::CreatePacket(ePacketType type, uint8 id, uint8 flags, const void* payload, uint32 payloadSize, uint16 seq)
	{
		return CreatePacketInternal(from_e(type), id, flags, payload, payloadSize, seq);
	}

	Sptr<SendBuffer> PacketBuilder::CreateSystemPacket(eSystemPacketId id, uint8 flags, const void* payload, uint32 payloadSize, uint16 seq)
	{
		return CreatePacketInternal(from_e(ePacketType::SYSTEM), from_e(id), flags, payload, payloadSize, seq);
	}

	Sptr<SendBuffer> PacketBuilder::CreateHandshakePacket(eSystemPacketId id)
	{
		return CreateSystemPacket(id, PacketFlags::NONE, nullptr, 0);
	}

	Sptr<SendBuffer> PacketBuilder::CreatePingPacket(const PING& ping, uint16 seq)
	{
		return CreateSystemPacket(eSystemPacketId::PING, PacketFlags::RELIABLE, &ping, sizeof(ping), seq);
	}

	Sptr<SendBuffer> PacketBuilder::CreatePongPacket(const PONG& pong, uint16 seq)
	{
		return CreateSystemPacket(eSystemPacketId::PING, PacketFlags::RELIABLE, &pong, sizeof(pong), seq);
	}

	Sptr<SendBuffer> PacketBuilder::CreateReliabilityAckPacket(uint16 seq, uint32 bitfield)
	{
		constexpr uint32 size = sizeof(PacketHeader) + sizeof(AckHeader);
		Sptr<SendBuffer> buf = SendBufferManager::Instance().Open(size);
		BufferWriter bw(buf->Buffer(), buf->AllocSize());



		buf->Close(bw.WriteSize());

		return buf;
	}

	Sptr<SendBuffer> PacketBuilder::CreatePacketInternal(uint8 type, uint8 id, uint8 flags, const void* payload, uint32 payloadSize, uint16 seq)
	{
		bool isReliable = HasFlag(flags, PacketFlags::RELIABLE);

		uint32 headerSize = isReliable ? PacketHeader::GetFullSize() : PacketHeader::GetBaseSize();
		const uint32 totalSize = headerSize + payloadSize;

		Sptr<SendBuffer> buf = SendBufferManager::Instance().Open(totalSize);
		BufferWriter bw(buf->Buffer(), buf->AllocSize());

		PacketHeader pkt(type, id, totalSize, flags, seq);

		if (isReliable)
		{
			bw.Write(pkt);
		}
		else
		{
			bw.WriteBytes(&pkt, PacketHeader::GetBaseSize());
		}

		if (payload && payloadSize != 0)
		{
			bw.WriteBytes(payload, payloadSize);
		}

		buf->Close(bw.WriteSize());
		return buf;
	}
}
