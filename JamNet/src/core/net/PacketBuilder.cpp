#include "pch.h"
#include "jamnet/core/net/PacketBuilder.h"


namespace jam::net
{
	std::shared_ptr<SendBuffer> PacketBuilder::CreatePacket(ePacketType type, uint8 id, uint8 flags, eChannelType channel, const void* payload, uint32 payloadSize, uint16 packetSeq, uint16 orderdSeq, uint8 fragIndex, uint8 fragTotal)
	{
		return CreatePacketInternal(E2U(type), id, flags, E2U(channel), payload, payloadSize, packetSeq, orderdSeq, fragIndex, fragTotal);
	}

	std::shared_ptr<SendBuffer> PacketBuilder::CreateSystemPacket(eSystemPacketId id, uint8 flags, eChannelType channel, const void* payload, uint32 payloadSize)
	{
		return CreatePacketInternal(E2U(ePacketType::SYSTEM), E2U(id), flags, E2U(channel), payload, payloadSize);
	}

	std::shared_ptr<SendBuffer> PacketBuilder::CreateHandshakePacket(eSystemPacketId id)
	{
		return CreateSystemPacket(id, PacketFlags::NONE, eChannelType::UNRELIABLE_UNORDERED, nullptr, 0);
	}

	std::shared_ptr<SendBuffer> PacketBuilder::CreatePingPacket(const PING_DATA& ping)
	{
		return CreateSystemPacket(eSystemPacketId::PING, PacketFlags::NONE, eChannelType::UNRELIABLE_UNORDERED, &ping, sizeof(PING_DATA));
	}

	std::shared_ptr<SendBuffer> PacketBuilder::CreatePongPacket(const PONG_DATA& pong)
	{
		return CreateSystemPacket(eSystemPacketId::PONG, PacketFlags::NONE, eChannelType::UNRELIABLE_UNORDERED, &pong, sizeof(PONG_DATA));
	}


	std::shared_ptr<SendBuffer> PacketBuilder::CreateAckPacket(const ACK_DATA& ack)
	{
		return CreatePacketInternal(E2U(ePacketType::ACK), E2U(eAckPacketId::ACK), PacketFlags::NONE, E2U(eChannelType::UNRELIABLE_SEQUENCED), &ack, sizeof(ACK_DATA));
	}

	std::shared_ptr<SendBuffer> PacketBuilder::CreateNackPacket(const NACK_DATA& nack)
	{
		return CreatePacketInternal(E2U(ePacketType::ACK), E2U(eAckPacketId::NACK), PacketFlags::NONE, E2U(eChannelType::UNRELIABLE_SEQUENCED), &nack, sizeof(NACK_DATA));
	}


	std::shared_ptr<SendBuffer> PacketBuilder::CreateRpcPacket(eRpcPacketId id, uint8 flags, eChannelType channel, const void* payload, uint32 payloadSize)
	{
		return CreatePacketInternal(E2U(ePacketType::RPC), E2U(id), flags, E2U(channel), payload, payloadSize);
	}

	RpcPacketOpenResult PacketBuilder::OpenRpcPacket(eRpcPacketId id, uint8 flags, eChannelType channel, uint32 payloadSize)
	{
		RpcPacketOpenResult out{};

		const bool isFragmented = HasFlag(flags, PacketFlags::FRAGMENTED);
		const uint32 headerSize = PacketHeader::CalcHeaderSize(channel, flags);

		const uint16 totalSize = headerSize + payloadSize;
		const uint16 allocSize = isFragmented ? totalSize : totalSize + sizeof(ACK_DATA);

		std::shared_ptr<SendBuffer> buf = SendBufferManager::Instance().Open(allocSize);
		BufferWriter bw(buf->Buffer(), buf->AllocSize());

		PacketHeader header(E2U(ePacketType::RPC), E2U(id), totalSize, flags, E2U(channel));
		bw.WriteBytes(&header, headerSize);

		out.buf			= buf;
		out.writer		= BufferWriter(buf->Buffer(), buf->AllocSize(), headerSize); 
		out.headerSize	= headerSize;
		out.totalSize	= totalSize;
		return out;
	}

	std::shared_ptr<SendBuffer> PacketBuilder::CreateCustomPacket(uint8 id, uint8 flags, eChannelType channel, const void* payload, uint32 payloadSize)
	{
		return CreatePacketInternal(E2U(ePacketType::CUSTOM), id, flags, E2U(channel), payload, payloadSize);
	}


	std::shared_ptr<SendBuffer> PacketBuilder::CreatePacketInternal(uint8 type, uint8 id, uint8 flags, uint8 channel, const void* payload, uint32 payloadSize, uint16 packetSeq, uint16 orderdSeq, uint8 fragIndex, uint8 fragTotal)
	{
		const bool isFragmented = HasFlag(flags, PacketFlags::FRAGMENTED);
		const eChannelType ch = U2E(eChannelType, channel);
		const uint32 headerSize = PacketHeader::CalcHeaderSize(ch, flags);

		const uint16 totalSize = headerSize + payloadSize;
		const uint16 allocSize = isFragmented ? totalSize : totalSize + sizeof(ACK_DATA);

		std::shared_ptr<SendBuffer> buf = SendBufferManager::Instance().Open(allocSize);
		BufferWriter bw(buf->Buffer(), buf->AllocSize());

		PacketHeader header(type, id, totalSize, flags, channel, packetSeq, orderdSeq, fragIndex, fragTotal);

		bw.WriteBytes(&header, headerSize);

		if (payload && payloadSize != 0)
			bw.WriteBytes(payload, payloadSize);

		if (isFragmented)
		{
			buf->Close(bw.WriteSize());
		}
		else
		{
			buf->CloseWithReserve(bw.WriteSize(), allocSize);
		}

		return buf;
	}
}
