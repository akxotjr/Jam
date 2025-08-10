#include "pch.h"
#include "PacketBuilder.h"

#include "Clock.h"


namespace jam::net
{
	Sptr<SendBuffer> PacketBuilder::CreateHandshakePacket(eHandshakePacketId id)
	{
		constexpr uint32 size = sizeof(PacketHeader) + sizeof(SysHeader);

		Sptr<SendBuffer> buf = SendBufferManager::Instance().Open(size);
		BufferWriter bw(buf->Buffer(), buf->AllocSize());

		PacketHeader pkt = {
			.sizeAndflags = MakeSizeAndFlags(size, 0),
			.type = static_cast<uint8>(ePacketType::SYSTEM)
		};

		SysHeader sys = {
			.sysId = static_cast<uint8>(id)
		};

		bw << pkt << sys;
		buf->Close(bw.WriteSize());

		return buf;
	}

	Sptr<SendBuffer> PacketBuilder::CreatePingPacket(uint16 seq)
	{
		constexpr uint32 size = sizeof(PacketHeader) + sizeof(RudpHeader) + sizeof(SysHeader) + sizeof(PING);

		Sptr<SendBuffer> buf = SendBufferManager::Instance().Open(size);
		BufferWriter bw(buf->Buffer(), buf->AllocSize());

		PacketHeader pkt = {
			.sizeAndflags = MakeSizeAndFlags(size, 0),
			.type = static_cast<uint8>(ePacketType::SYSTEM)
		};

		RudpHeader rudp = {
			.sequence = seq
		};

		SysHeader sys = {
			.sysId = static_cast<uint8>(eSysPacketId::C_PING)
		};

		PING payload = {
			.clientSendTick = Clock::Instance().GetCurrentTick()
		};

		bw << pkt << rudp << sys << payload;
		buf->Close(bw.WriteSize());

		return buf;
	}

	Sptr<SendBuffer> PacketBuilder::CreatePongPacket(uint16 seq, uint64 clientSendTick)
	{
		constexpr uint32 size = sizeof(PacketHeader) + sizeof(RudpHeader) + sizeof(SysHeader) + sizeof(PONG);

		Sptr<SendBuffer> buf = SendBufferManager::Instance().Open(size);
		BufferWriter bw(buf->Buffer(), buf->AllocSize());

		PacketHeader pkt = {
			.sizeAndflags = MakeSizeAndFlags(size, 0),
			.type = static_cast<uint8>(ePacketType::SYSTEM)
		};

		RudpHeader rudp = {
			.sequence = seq
		};

		SysHeader sys = {
			.sysId = static_cast<uint8>(eSysPacketId::S_PONG)
		};

		PONG payload = {
			.clientSendTick = clientSendTick,
			.serverSendTick = Clock::Instance().GetCurrentTick()
		};

		bw << pkt << rudp << sys << payload;
		buf->Close(bw.WriteSize());

		return buf;
	}

	Sptr<SendBuffer> PacketBuilder::CreateAckPacket(uint16 seq, uint32 bitfield)
	{
		constexpr uint32 size = sizeof(PacketHeader) + sizeof(AckHeader);

		Sptr<SendBuffer> buf = SendBufferManager::Instance().Open(size);
		BufferWriter bw(buf->Buffer(), buf->AllocSize());

		PacketHeader pkt = {
			.sizeAndflags = MakeSizeAndFlags(size, 0),
			.type = static_cast<uint8>(ePacketType::ACK)
		};

		AckHeader ack = {
			.latestSeq = seq,
			.bitfield = bitfield
		};

		bw << pkt << ack;
		buf->Close(bw.WriteSize());

		return buf;
	}





}
