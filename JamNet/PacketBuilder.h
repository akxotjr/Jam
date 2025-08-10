#pragma once
#include "BufferReader.h"
#include "BufferWriter.h"

namespace jam::net
{
	enum class eHandshakePacketId : uint8;
}

namespace jam::net
{
	struct PacketHeader;
	struct RudpHeader;
	struct FragmentHeader;
	struct SysHeader;
	struct RpcHeader;
	struct AckHeader;


	class PacketBuilder
	{
	public:
		// System Packets
		static Sptr<SendBuffer> CreateHandshakePacket(eHandshakePacketId id);
		static Sptr<SendBuffer> CreatePingPacket(uint16 seq);
		static Sptr<SendBuffer> CreatePongPacket(uint16 seq, uint64 clientSendTick);

		// Ack Packets
		static Sptr<SendBuffer> CreateAckPacket(uint16 seq, uint32 bitfield);

		// Rpc Packets
		static Sptr<SendBuffer> CreateRpcPacket();

		// Custom Packets
		static Sptr<SendBuffer> CreateCustomPacket();

	};
}

