#pragma once

namespace jam::net
{
	constexpr uint16 JAMNET_MTU = 1400;
	inline constexpr size_t kMaxUdpScatterParts = 16;
	inline constexpr uint8 kMaxAckRanges = 8;


	enum class ePacketType : uint8
	{
		SYSTEM	= 0,	// 0b 00
		ACK		= 1,	// 0b 01
		RPC		= 2,	// 0b 10
		CUSTOM	= 3		// 0b 11
	};

	enum class ePacketGroup : uint8
	{
		CTRL	= 0,	// SYSTEM, ACK
		NORMAL	= 1	// RPC, CUSTOM
	};

	enum class eSystemPacketId : uint8
	{
		PING				= 10,
		PONG				= 11,

		TCP_BIND_REQ		= 22,
		TCP_BIND_RES		= 23,
		UDP_BIND_REQ		= 24,
		UDP_BIND_RES		= 25,
		UDP_UNBIND_REQ		= 26,
		UDP_UNBIND_RES		= 27,
	};

	enum class eBootstrapKind : uint8
	{
		Pending,
		Fresh,
		Resync,
	};

	enum class eAckPacketId : uint8
	{
		ACK					= 1,
	};


	enum class eChannel : uint8
	{
		TCP_DEFAULT				= 0,
		UDP_DEFAULT				= 1,
		UNRELIABLE_SEQUENCED	= 2,
		RELIABLE_UNORDERED		= 3,
		RELIABLE_ORDERED		= 4,
	};

	constexpr bool IsTcp(eChannel channel)
	{
		return channel == eChannel::TCP_DEFAULT;
	}

	constexpr bool IsReliableChannel(eChannel channel)
	{
		return channel == eChannel::RELIABLE_ORDERED || channel == eChannel::RELIABLE_UNORDERED;
	}

	constexpr bool IsOrderedChannel(eChannel channel)
	{
		return channel == eChannel::RELIABLE_ORDERED;
	}

	constexpr bool HasRecencySequence(eChannel channel)
	{
		return channel == eChannel::UNRELIABLE_SEQUENCED;
	}

	constexpr bool HasReliabilitySequence(eChannel channel)
	{
		return IsReliableChannel(channel);
	}

	constexpr bool HasOrderSequence(eChannel channel)
	{
		return channel == eChannel::RELIABLE_ORDERED;
	}

	namespace PacketFlags
	{
		constexpr uint8 NONE = 0x00;		// 0b 0000
		constexpr uint8 COMPRESSED = 0x01;		// 0b 0001
		constexpr uint8 ENCRYPTED = 0x02;		// 0b 0010
		constexpr uint8 FRAGMENTED = 0x04;		// 0b 0100
	}

	constexpr bool	HasFlag(uint8 flags, uint8 flag) { return (flags & flag) != 0; }
	constexpr uint8 SetFlag(uint8 flags, uint8 flag) { return flags | flag; }
	constexpr uint8 ClearFlag(uint8 flags, uint8 flag) { return flags & ~flag; }




#pragma pack(push, 1)
	struct AckHeader
	{
		uint16	largestAck		= 0;	// Most recently received reliability sequence; the reference point for decoding AckRange entries.
		uint16	cumulativeAck	= 0;	// Indicates that all packets up to this sequence were received contiguously; valid when HasCumulativeAck is set.
		uint8	flags			= 0;	// Flags for optional information included in the ACK header.
		uint8	rangesCount		= 0;	// Number of AckRange entries following the header.
		uint32	ackDelay_us		= 0;	// Delay between receiving the acknowledged packet and sending this ACK, in microseconds.
	};

	struct AckRange
	{
		uint16 gap		= 0;	// Number of unacknowledged sequences between the previous and current ACK ranges; 0 for the first range, which starts at largestAck.
		uint16 length	= 0;	// Number of contiguous acknowledged sequences in the current ACK range.
	};

#pragma pack(pop)

	namespace AckFlags
	{
		inline constexpr uint8 HasCumulativeAck = 0x01;
	}


#pragma pack(push, 1)
	inline constexpr size_t kMaxAuthFieldBytes = 256;

	struct TCP_BIND_REQ_DATA
	{
		uint32 scheme = 0;

		uint16 field0Size = 0;
		uint16 field1Size = 0;
		uint8  field0[kMaxAuthFieldBytes] = {};
		uint8  field1[kMaxAuthFieldBytes] = {};
	};

	struct TCP_BIND_RES_DATA
	{
		uint64 accountId = 0;
		uint64 userId	 = 0;
		uint64 sessionId = 0;
		uint8  success	 = 0;
		eBootstrapKind bootstrapKind = eBootstrapKind::Pending;
	};

	struct UDP_BIND_REQ_DATA
	{
		uint64 accountId	 = 0;
		uint64 userId		 = 0;
		uint64 transactionId = 0;
	};

	struct UDP_BIND_RES_DATA
	{
		uint64 accountId	 = 0;
		uint64 userId		 = 0;
		uint64 sessionId	 = 0;
		uint64 transactionId = 0;
		uint8  success		 = 0;
	};

	struct UDP_UNBIND_REQ_DATA
	{
		uint64 sessionId	 = 0;
		uint64 transactionId = 0;
	};

	struct UDP_UNBIND_RES_DATA
	{
		uint64 sessionId	 = 0;
		uint64 transactionId = 0;
	};
#pragma pack(pop)




#pragma pack(push, 1)
	struct PING_DATA
	{
		uint64		t1Wire_ns		= 0;
		uint64		t1App_ns		= 0;
		uint64		prev_t3_server_send_ns	= 0;
		uint64		prev_t4_client_recv_ns	= 0;
	};
#pragma pack(pop)

#pragma pack(push, 1)
	struct PONG_DATA
	{
		uint64		t1Wire_ns		= 0;
		uint64		t2Wire_ns		= 0;
		uint64		t3Wire_ns		= 0;

		uint64		t1App_ns		= 0;
		uint64		t2App_ns		= 0;
		uint64		t3App_ns		= 0;
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
		constexpr uint8 NONE     = 0x00;
		constexpr uint8 REQUEST  = 0x01;
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
