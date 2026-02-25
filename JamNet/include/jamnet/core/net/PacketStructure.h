#pragma once

namespace jam::net
{
	constexpr uint16 JAMNET_MTU = 1400;


	enum class ePacketType : uint8
	{
		SYSTEM = 0,	// 0b 00
		ACK = 1,	// 0b 01
		RPC = 2,	// 0b 10
		CUSTOM = 3	// 0b 11
	};

	enum class ePacketGroup : uint8
	{
		CTRL = 0,	// SYSTEM, ACK
		NORMAL = 1	// RPC, CUSTOM
	};

	enum class eSystemPacketId : uint8
	{
		CONNECT_SYN = 1,
		CONNECT_SYNACK = 2,
		CONNECT_ACK = 3,
		DISCONNECT_FIN = 4,
		DISCONNECT_FINACK = 5,
		DISCONNECT_ACK = 6,

		PING = 10,
		PONG = 11,
		HEARTBEAT = 12,

		SERVER_INFO = 20,
		CLIENT_INFO = 21,
	};

	enum class eAckPacketId : uint8
	{
		ACK = 1,
		NACK = 2
	};



	enum class eChannelType : uint8
	{
		TCP_DEFAULT = 0,

		UNRELIABLE_UNORDERED = 0,
		RELIABLE_ORDERED = 1,
		UNRELIABLE_SEQUENCED = 2,
		RELIABLE_UNORDERED = 3
	};

	constexpr bool IsTcp(eChannelType channel)
	{
		return channel == eChannelType::TCP_DEFAULT;
	}

	constexpr bool HasReliable(eChannelType channel)
	{
		return channel == eChannelType::RELIABLE_ORDERED || channel == eChannelType::RELIABLE_UNORDERED;
	}

	constexpr bool HasSequence(eChannelType channel)
	{
		if (channel == eChannelType::TCP_DEFAULT || channel == eChannelType::UNRELIABLE_UNORDERED)
			return false;
		return true;
	}

	namespace PacketFlags
	{
		constexpr uint8 NONE = 0x00;		// 0b 0000	
		constexpr uint8 COMPRESSED = 0x01;		// 0b 0001
		constexpr uint8 ENCRYPTED = 0x02;		// 0b 0010
		constexpr uint8 FRAGMENTED = 0x04;		// 0b 0100
		constexpr uint8 PIGGYBACK_ACK = 0x08;		// 0b 1000
	}

	constexpr bool	HasFlag(uint8 flags, uint8 flag) { return (flags & flag) != 0; }
	constexpr uint8 SetFlag(uint8 flags, uint8 flag) { return flags | flag; }
	constexpr uint8 ClearFlag(uint8 flags, uint8 flag) { return flags & ~flag; }




#pragma pack(push, 1)
	struct ACK_DATA
	{
		uint16 latestSeq = 0;
		uint32 wnd;
	};
#pragma pack(pop)

#pragma pack(push, 1)
	struct NACK_DATA
	{
		uint16 missingSeq;
		uint32 wnd;
	};
#pragma pack(pop)





#pragma pack(push, 1)
	struct PING_DATA
	{
		uint64		t1_client_send_ns		= 0;		
		uint64		prev_t3_server_send_ns	= 0;	
		uint64		prev_t4_client_recv_ns	= 0;	
	};
#pragma pack(pop)

#pragma pack(push, 1)
	struct PONG_DATA
	{
		uint64		t1_client_send_ns		= 0;			
		uint64		t2_server_recv_ns		= 0;						
		uint64		t3_server_send_ns		= 0;						
	};
#pragma pack(pop)



	enum class eRpcPacketId : uint8
	{
		FLATBUFFER_RPC = 1,
		PROTOBUF_RPC = 2,
		JSON_RPC = 3,
		BINARY_RPC = 4,
	};

	namespace RpcFlags
	{
		constexpr uint8 NONE = 0x00;
		constexpr uint8 REQUEST = 0x01;
		constexpr uint8 RESPONSE = 0x02;
	}

#pragma pack(push, 1)
	struct RpcHeader
	{
		uint16 rpcId;
		uint32 requestId;
		uint8  flags;
	};
#pragma pack(pop)

	static_assert(sizeof(RpcHeader) == 7, "RpcHeader must be packed(1) and 7 bytes");

}
