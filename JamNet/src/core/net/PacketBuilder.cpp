#include "pch.h"
#include "jamnet/core/net/PacketBuilder.h"


namespace jam::net
{
	namespace
	{
		bool ValidatePacketBuild(uint32 visibleSize, const void* payload, uint32 payloadSize)
		{
			if (payloadSize != 0 && payload == nullptr)
			{
				JAMNET_LOG_ERROR("[PacketBuilder] payload is null. payloadSize={}", payloadSize);
				return false;
			}

			if (visibleSize > PacketHeader::MAX_PACKET_SIZE)
			{
				JAMNET_LOG_ERROR("[PacketBuilder] packet too large. visibleSize={}, max={}", visibleSize, PacketHeader::MAX_PACKET_SIZE);
				return false;
			}

			return true;
		}

		Packet CreateHeaderOnlyPacket(const PacketHeader& header, uint32 headerSize)
		{
			BufWriter writer(GetNetBufferPool(eNetBufferPoolKind::Packet));
			BufferSlice slice = writer.OpenForPayload(headerSize, alignof(PacketHeader));

			WritePayload(slice, &header, headerSize);
			slice.Close();

			return MakeOwned(slice);
		}
	}

	Packet PacketBuilder::CreatePacket(ePacketType type, uint8 id, uint8 flags, eChannel channel, const void* payload, uint32 payloadSize, uint16 packetSeq, uint16 orderdSeq, uint8 fragIndex, uint8 fragTotal)
	{
		return CreatePacketInternal(E2U(type), id, flags, E2U(channel), payload, payloadSize, packetSeq, orderdSeq, fragIndex, fragTotal);
	}

	Packet PacketBuilder::CreateSystemPacket(eSystemPacketId id, uint8 flags, eChannel channel, const void* payload, uint32 payloadSize)
	{
		return CreatePacketInternal(E2U(ePacketType::SYSTEM), E2U(id), flags, E2U(channel), payload, payloadSize);
	}

	Packet PacketBuilder::CreateHandshakePacket(eSystemPacketId id)
	{
		return CreateSystemPacket(id, PacketFlags::NONE, eChannel::UNRELIABLE_UNORDERED, nullptr, 0);
	}

	Packet PacketBuilder::CreatePingPacket(const PING_DATA& ping)
	{
		return CreateSystemPacket(eSystemPacketId::PING, PacketFlags::NONE, eChannel::UNRELIABLE_UNORDERED, &ping, sizeof(PING_DATA));
	}

	Packet PacketBuilder::CreatePongPacket(const PONG_DATA& pong)
	{
		return CreateSystemPacket(eSystemPacketId::PONG, PacketFlags::NONE, eChannel::UNRELIABLE_UNORDERED, &pong, sizeof(PONG_DATA));
	}

	
	Packet PacketBuilder::CreateAckPacket(const ACK_DATA& ack)
	{
		return CreatePacketInternal(E2U(ePacketType::ACK), E2U(eAckPacketId::ACK), PacketFlags::NONE, E2U(eChannel::UNRELIABLE_SEQUENCED), &ack, sizeof(ACK_DATA));
	}

	Packet PacketBuilder::CreateNackPacket(const NACK_DATA& nack)
	{
		return CreatePacketInternal(E2U(ePacketType::ACK), E2U(eAckPacketId::NACK), PacketFlags::NONE, E2U(eChannel::UNRELIABLE_SEQUENCED), &nack, sizeof(NACK_DATA));
	}

	Packet PacketBuilder::CreateRpcPacket(const RpcHeader* rpc, const void* payload, uint32 payloadSize, uint8 flags, eChannel ch)
	{
		if (!rpc)
		{
			JAMNET_LOG_ERROR("[PacketBuilder] rpc header is null");
			return {};
		}

		const uint32 headerSize   = PacketHeader::CalcHeaderSize(ch, flags);

		const uint32 visibleSize = headerSize + sizeof(RpcHeader) + payloadSize;
		if (!ValidatePacketBuild(visibleSize, payload, payloadSize))
			return {};

		BufWriter writer(GetNetBufferPool(eNetBufferPoolKind::Packet));
		BufferSlice slice = writer.OpenForPacket(sizeof(RpcHeader) + payloadSize, headerSize, alignof(PacketHeader));

		PacketHeader header(E2U(ePacketType::RPC), E2U(eRpcPacketId::FLATBUFFER_RPC), static_cast<uint16>(visibleSize), flags, E2U(ch), 0, 0, 0, 0);
		WriteHeader(slice, header, headerSize);

		if (rpc)
			WritePayload(slice, rpc, sizeof(RpcHeader));

		if (payload && payloadSize != 0)
			WritePayload(slice, payload, payloadSize);

		slice.Close();

		return MakeOwned(slice);
	}

	Packet PacketBuilder::CreateCustomPacket(uint8 id, uint8 flags, eChannel channel, const void* payload, uint32 payloadSize)
	{
		return CreatePacketInternal(E2U(ePacketType::CUSTOM), id, flags, E2U(channel), payload, payloadSize);
	}


	Packet PacketBuilder::CreatePacketInternal(uint8 type, uint8 id, uint8 flags, uint8 ch, const void* payload, uint32 payloadSize, uint16 packetSeq, uint16 orderedSeq, uint8 fragIndex, uint8 fragTotal)
	{
		const eChannel channel	    = U2E(eChannel, ch);
		const uint32   headerSize   = PacketHeader::CalcHeaderSize(channel, flags);

		const uint32 visibleSize = headerSize + payloadSize;
		if (!ValidatePacketBuild(visibleSize, payload, payloadSize))
			return {};

		PacketHeader header(type, id, static_cast<uint16>(visibleSize), flags, ch, packetSeq, orderedSeq, fragIndex, fragTotal);
		if (payloadSize == 0)
			return CreateHeaderOnlyPacket(header, headerSize);

		BufWriter writer(GetNetBufferPool(eNetBufferPoolKind::Packet));
		BufferSlice slice = writer.OpenForPacket(payloadSize, headerSize, alignof(PacketHeader));

		WriteHeader(slice, header, headerSize);

		if (payload && payloadSize != 0)
			WritePayload(slice, payload, payloadSize);

		slice.Close();

		return MakeOwned(slice);
	}
}
