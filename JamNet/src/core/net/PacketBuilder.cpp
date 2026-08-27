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
				JAM_LOG_ERROR("[PacketBuilder] payload is null. payloadSize={}", payloadSize);
				return false;
			}

			if (visibleSize > PacketHeader::MAX_PACKET_SIZE)
			{
				JAM_LOG_ERROR("[PacketBuilder] packet too large. visibleSize={}, max={}", visibleSize, PacketHeader::MAX_PACKET_SIZE);
				return false;
			}

			return true;
		}

		Packet CreateHeaderOnlyPacket(const PacketHeader& header, uint32 headerSize)
		{
			BufWriter writer(GetPacketBufferPool(headerSize));
			BufferSlice slice = writer.OpenForPayload(headerSize, alignof(PacketHeader));

			WritePayload(slice, &header, headerSize);
			slice.Close();

			return MakeOwned(slice);
		}
	}

	Packet PacketBuilder::CreatePacket(ePacketType type, uint8 id, uint8 flags, eChannel channel, const void* payload, uint32 payloadSize, uint16 recencySeq, uint16 reliabilitySeq, uint16 orderSeq, uint8 fragIndex, uint8 fragTotal)
	{
		return CreatePacketInternal(E2U(type), id, flags, E2U(channel), payload, payloadSize, recencySeq, reliabilitySeq, orderSeq, fragIndex, fragTotal);
	}

	Packet PacketBuilder::CreateSystemPacket(eSystemPacketId id, uint8 flags, eChannel channel, const void* payload, uint32 payloadSize)
	{
		return CreatePacketInternal(E2U(ePacketType::SYSTEM), E2U(id), flags, E2U(channel), payload, payloadSize);
	}

	Packet PacketBuilder::CreateTcpBindReqPacket(const TCP_BIND_REQ_DATA& req)
	{
		return CreateSystemPacket(eSystemPacketId::TCP_BIND_REQ, PacketFlags::NONE, eChannel::TCP_DEFAULT, &req, sizeof(TCP_BIND_REQ_DATA));
	}

	Packet PacketBuilder::CreateTcpBindResPacket(const TCP_BIND_RES_DATA& res)
	{
		return CreateSystemPacket(eSystemPacketId::TCP_BIND_RES, PacketFlags::NONE, eChannel::TCP_DEFAULT, &res, sizeof(TCP_BIND_RES_DATA));
	}

	Packet PacketBuilder::CreateUdpBindReqPacket(const UDP_BIND_REQ_DATA& req)
	{
		return CreateSystemPacket(eSystemPacketId::UDP_BIND_REQ, PacketFlags::NONE, eChannel::UDP_DEFAULT, &req, sizeof(UDP_BIND_REQ_DATA));
	}

	Packet PacketBuilder::CreateUdpBindResPacket(const UDP_BIND_RES_DATA& res)
	{
		return CreateSystemPacket(eSystemPacketId::UDP_BIND_RES, PacketFlags::NONE, eChannel::UDP_DEFAULT, &res, sizeof(UDP_BIND_RES_DATA));
	}

	Packet PacketBuilder::CreateUdpBindConfirmPacket(const UDP_BIND_CONFIRM_DATA& confirm)
	{
		return CreateSystemPacket(eSystemPacketId::UDP_BIND_CONFIRM, PacketFlags::NONE, eChannel::UDP_DEFAULT, &confirm, sizeof(UDP_BIND_CONFIRM_DATA));
	}

	Packet PacketBuilder::CreateUdpUnbindReqPacket(const UDP_UNBIND_REQ_DATA& req)
	{
		return CreateSystemPacket(eSystemPacketId::UDP_UNBIND_REQ, PacketFlags::NONE, eChannel::UDP_DEFAULT, &req, sizeof(UDP_UNBIND_REQ_DATA));
	}

	Packet PacketBuilder::CreateUdpUnbindResPacket(const UDP_UNBIND_RES_DATA& res)
	{
		return CreateSystemPacket(eSystemPacketId::UDP_UNBIND_RES, PacketFlags::NONE, eChannel::UDP_DEFAULT, &res, sizeof(UDP_UNBIND_RES_DATA));
	}

	Packet PacketBuilder::CreatePingPacket(const PING_DATA& ping)
	{
		return CreateSystemPacket(eSystemPacketId::PING, PacketFlags::NONE, eChannel::UDP_DEFAULT, &ping, sizeof(PING_DATA));
	}

	Packet PacketBuilder::CreatePongPacket(const PONG_DATA& pong)
	{
		return CreateSystemPacket(eSystemPacketId::PONG, PacketFlags::NONE, eChannel::UDP_DEFAULT, &pong, sizeof(PONG_DATA));
	}

	
	Packet PacketBuilder::CreateAckPacket(const ACK_DATA& ack)
	{
		return CreatePacketInternal(E2U(ePacketType::ACK), E2U(eAckPacketId::ACK), PacketFlags::NONE, E2U(eChannel::UDP_DEFAULT), &ack, sizeof(ACK_DATA));
	}


	Packet PacketBuilder::CreateRpcPacket(const RpcHeader* rpc, const void* payload, uint32 payloadSize, uint8 flags, eChannel ch)
	{
		if (!rpc)
		{
			JAM_LOG_ERROR("[PacketBuilder] rpc header is null");
			return {};
		}

		const uint32 headerSize   = PacketHeader::CalcHeaderSize(ch, flags);

		const uint32 visibleSize = headerSize + sizeof(RpcHeader) + payloadSize;
		if (!ValidatePacketBuild(visibleSize, payload, payloadSize))
			return {};

		BufWriter writer(GetPacketBufferPool(visibleSize));
		BufferSlice slice = writer.OpenForPacket(sizeof(RpcHeader) + payloadSize, headerSize, alignof(PacketHeader));

		PacketHeader header(E2U(ePacketType::RPC), E2U(eRpcPacketId::FLATBUFFER_RPC), static_cast<uint16>(visibleSize), flags, E2U(ch), 0, 0, 0, 0, 0);
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


	Packet PacketBuilder::CreatePacketInternal(uint8 type, uint8 id, uint8 flags, uint8 ch, const void* payload, uint32 payloadSize, uint16 recencySeq, uint16 reliabilitySeq, uint16 orderSeq, uint8 fragIndex, uint8 fragTotal)
	{
		const eChannel channel	    = U2E(eChannel, ch);
		const uint32   headerSize   = PacketHeader::CalcHeaderSize(channel, flags);

		const uint32 visibleSize = headerSize + payloadSize;
		if (!ValidatePacketBuild(visibleSize, payload, payloadSize))
			return {};

		PacketHeader header(type, id, static_cast<uint16>(visibleSize), flags, ch, recencySeq, reliabilitySeq, orderSeq, fragIndex, fragTotal);
		if (payloadSize == 0)
			return CreateHeaderOnlyPacket(header, headerSize);

		BufWriter writer(GetPacketBufferPool(visibleSize));
		BufferSlice slice = writer.OpenForPacket(payloadSize, headerSize, alignof(PacketHeader));

		WriteHeader(slice, header, headerSize);

		if (payload && payloadSize != 0)
			WritePayload(slice, payload, payloadSize);

		slice.Close();

		return MakeOwned(slice);
	}
}
