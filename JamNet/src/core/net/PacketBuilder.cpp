#include "pch.h"
#include "jamnet/core/net/PacketBuilder.h"


namespace jam::net
{
	shared_ptr<SendBuffer> PacketBuilder::CreatePacket(ePacketType type, uint8 id, uint8 flags, eChannelType channel, const void* payload, uint32 payloadSize, uint16 seq, uint8 fragIndex, uint8 fragTotal)
	{
		return CreatePacketInternal(E2U(type), id, flags, E2U(channel), payload, payloadSize, seq, fragIndex, fragTotal);
	}

	shared_ptr<SendBuffer> PacketBuilder::CreateSystemPacket(eSystemPacketId id, uint8 flags, eChannelType channel, const void* payload, uint32 payloadSize)
	{
		return CreatePacketInternal(E2U(ePacketType::SYSTEM), E2U(id), flags, E2U(channel), payload, payloadSize);
	}

	shared_ptr<SendBuffer> PacketBuilder::CreateHandshakePacket(eSystemPacketId id)
	{
		return CreateSystemPacket(id, PacketFlags::NONE, eChannelType::RELIABLE_ORDERED, nullptr, 0);
	}

	shared_ptr<SendBuffer> PacketBuilder::CreatePingPacket(const PING_DATA& ping)
	{
		return CreateSystemPacket(eSystemPacketId::PING, PacketFlags::NONE, eChannelType::RELIABLE_ORDERED, &ping, sizeof(PING_DATA));
	}

	shared_ptr<SendBuffer> PacketBuilder::CreatePongPacket(const PONG_DATA& pong)
	{
		return CreateSystemPacket(eSystemPacketId::PONG, PacketFlags::NONE, eChannelType::RELIABLE_ORDERED, &pong, sizeof(PONG_DATA));
	}


	shared_ptr<SendBuffer> PacketBuilder::CreateAckPacket(const ACK_DATA& ack)
	{
		return CreatePacketInternal(E2U(ePacketType::ACK), E2U(eAckPacketId::ACK), PacketFlags::NONE, E2U(eChannelType::UNRELIABLE_UNORDERED), &ack, sizeof(ACK_DATA));
	}

	shared_ptr<SendBuffer> PacketBuilder::CreateNackPacket(const NACK_DATA& nack)
	{
		return CreatePacketInternal(E2U(ePacketType::ACK), E2U(eAckPacketId::NACK), PacketFlags::NONE, E2U(eChannelType::UNRELIABLE_UNORDERED), &nack, sizeof(NACK_DATA));
	}



	shared_ptr<SendBuffer> PacketBuilder::CreateRpcPacket(eRpcPacketId id, uint8 flags, eChannelType channel, const void* payload, uint32 payloadSize)
	{
		return CreatePacketInternal(E2U(ePacketType::RPC), E2U(id), flags, E2U(channel), payload, payloadSize);
	}

	RpcPacketOpenResult PacketBuilder::OpenRpcPacket(eRpcPacketId id, uint8 flags, eChannelType channel, uint32 payloadSize)
	{
		RpcPacketOpenResult out{};

		const bool isFragmented = HasFlag(flags, PacketFlags::FRAGMENTED);
		const bool hasSeq = HasSequence(channel);

		uint32 headerSize;
		if (isFragmented)
			headerSize = PacketHeader::FULL_SIZE;
		else if (hasSeq)
			headerSize = PacketHeader::HALF_SIZE;
		else
			headerSize = PacketHeader::BASE_SIZE;

		const uint16 totalSize = headerSize + payloadSize;
		const uint16 allocSize = isFragmented ? totalSize : totalSize + sizeof(ACK_DATA);

		shared_ptr<SendBuffer> buf = SendBufferManager::Instance().Open(allocSize);
		BufferWriter bw(buf->Buffer(), buf->AllocSize());

		PacketHeader header(E2U(ePacketType::RPC), E2U(id), totalSize, flags, E2U(channel));
		bw.WriteBytes(&header, headerSize);

		out.buf = buf;
		out.writer = BufferWriter(buf->Buffer(), buf->AllocSize(), headerSize); // payload부터 쓰도록
		out.headerSize = headerSize;
		out.totalSize = totalSize;
		return out;
	}

	shared_ptr<SendBuffer> PacketBuilder::CreateCustomPacket(uint8 id, uint8 flags, eChannelType channel, const void* payload, uint32 payloadSize)
	{
		return CreatePacketInternal(E2U(ePacketType::CUSTOM), id, flags, E2U(channel), payload, payloadSize);
	}



	shared_ptr<SendBuffer> PacketBuilder::CreatePacketInternal(uint8 type, uint8 id, uint8 flags, uint8 channel, const void* payload, uint32 payloadSize, uint16 seq, uint8 fragIndex, uint8 fragTotal)
	{
		const bool isFragmented = HasFlag(flags, PacketFlags::FRAGMENTED);

		const eChannelType ch = U2E(eChannelType, channel);
		const bool hasSeq = HasSequence(ch);

		uint32 headerSize;
		if (isFragmented)
			headerSize = PacketHeader::FULL_SIZE;
		else if (hasSeq)
			headerSize = PacketHeader::HALF_SIZE;
		else
			headerSize = PacketHeader::BASE_SIZE;

		const uint16 totalSize = headerSize + payloadSize;
		const uint16 allocSize = isFragmented ? totalSize : totalSize + sizeof(ACK_DATA);

		shared_ptr<SendBuffer> buf = SendBufferManager::Instance().Open(allocSize);
		BufferWriter bw(buf->Buffer(), buf->AllocSize());

		PacketHeader header(type, id, totalSize, flags, channel, seq, fragIndex, fragTotal);

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
