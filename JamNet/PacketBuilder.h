#pragma once


namespace jam::net
{
	enum class eRpcPacketId : uint8;
}

namespace jam::net
{
	struct RudpHeader;
	struct FragmentHeader;
	struct SysHeader;
	struct RpcHeader;
	struct AckHeader;

	enum class ePacketType : uint8
	{
		SYSTEM = 0,	// 00
		ACK = 1,	// 01
		RPC = 2,	// 10
		CUSTOM = 3	// 11
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
		RELIABILITY_ACK = 1,
		SELECTIVE_ACK = 2,
		CUMULATIVE_ACK = 3,
		FAST_RETRANSMIT = 4,
	};

	

	namespace PacketFlags
	{
		constexpr uint8 NONE = 0x00;			
		constexpr uint8 RELIABLE = 0x01;		// 0b00001	
		constexpr uint8 COMPRESSED = 0x02;		// 0b00010
		constexpr uint8 ENCRYPTED = 0x04;		// 0b00100
		constexpr uint8 FRAGMENTED = 0x08;		// 0b01000
		constexpr uint8 PIGGYBACK_ACK = 0x10;	// 0b10000
	}

	constexpr bool	HasFlag(uint8 flags, uint8 flag) { return (flags & flag) != 0; }
	constexpr uint8 SetFlag(uint8 flags, uint8 flag) { return flags | flag; }
	constexpr uint8 ClearFlag(uint8 flags, uint8 flag) { return flags & ~flag; }

	constexpr uint16 MAX_PAYLOAD_SIZE = 1024;

#pragma pack(push, 1)
	struct PacketHeader
	{
	public:
		PacketHeader() : data(0), sequence(0), fragmentData(0) {}
		PacketHeader(uint8 type, uint8 id, uint16 size, uint8 flags, uint16 seq = 0, uint8 fragIndex = 0, uint8 fragTotal = 0)
		{
			data = ((type & 0x03) << 22) |
				((id & 0x3F) << 16) |
				((size & 0x7FF) << 5) |
				(flags & 0x1F);
			sequence = seq;
			SetFragmentInfo(fragIndex, fragTotal);
		}


		uint8		GetType() const { return (data >> 22) & 0x03; }				// 2 bit
		uint8		GetId() const { return (data >> 16) & 0x3F; }				// 6 bit  
		uint16		GetSize() const { return (data >> 5) & 0x7FF; }				// 11 bit
		uint8		GetFlags() const { return data & 0x1F; }					// 5 bit
		uint16		GetSequence() const { return sequence; }					// 16 bit
		uint8		GetFragmentIndex() const { return (fragmentData >> 8) & 0xFF; }
		uint8		GetTotalFragments() const { return fragmentData & 0xFF; }

		void		SetType(uint8 type) { data = (data & ~(0x03 << 22)) | ((type & 0x03) << 22); }
		void		SetId(uint8 id) { data = (data & ~(0x3F << 16)) | ((id & 0x3F) << 16); }
		void		SetSize(uint16 size) { data = (data & ~(0x7FF << 5)) | ((size & 0x7FF) << 5); }
		void		SetFlags(uint8 flags) { data = (data & ~0x1F) | (flags & 0x1F); }
		void		SetSequence(uint16 seq) { sequence = seq; }
		void		SetFragmentInfo(uint8 index, uint8 total) { fragmentData = (index << 8) | total; }

		bool		IsReliable() const { return HasFlag(GetFlags(), PacketFlags::RELIABLE); }
		bool		IsFragmented() const { return HasFlag(GetFlags(), PacketFlags::FRAGMENTED); }

		static constexpr uint32 GetBaseSize() { return 3; }			// only data : 3 bytes
		static constexpr uint32 GetHalfSize() { return 5; }			// data + sequence : 5 bytes
		static constexpr uint32 GetFullSize() { return 7; }			// data + sequence + fragment : 7 bytes

		uint32		GetActualSize() const { return IsReliable() ? GetHalfSize() : GetBaseSize(); }

	private:
		uint32		data : 24;
		uint16		sequence;
		uint16		fragmentData;
	};
#pragma pop




	struct PacketAnalysis
	{
		bool			IsReliable() const { return header.IsReliable(); }
		bool			IsFragmented() const { return header.IsFragmented(); }

		ePacketType		GetType() const { return U2E(ePacketType, header.GetType()); }
		uint8			GetId() const { return header.GetId(); }
		uint16			GetSequence() const { return header.GetSequence(); }
		uint8			GetFragmentIndex() const { return header.GetFragmentIndex(); }
		uint8			GetTotalFragments() const { return header.GetTotalFragments(); }

		BYTE*			GetPayloadPtr(BYTE* buf) const { return buf + headerSize; }


		bool			isValid = false;
		PacketHeader	header;
		uint32			headerSize = 0;
		uint32			payloadSize = 0;
		uint32			totalSize = 0;
	};



	class PacketBuilder
	{
	public:
		static Sptr<SendBuffer> CreatePacket(ePacketType type, uint8 id, uint8 flags = PacketFlags::NONE, const void* payload = nullptr, uint32 payloadSize = 0, uint16 seq = 0, uint8 fragIndex = 0, uint8 fragTotal = 0);

		// System Packets
		static Sptr<SendBuffer> CreateSystemPacket(eSystemPacketId id, uint8 flags = PacketFlags::NONE, const void* payload = nullptr, uint32 payloadSize = 0, uint16 seq = 0);

		static Sptr<SendBuffer> CreateHandshakePacket(eSystemPacketId id);
		static Sptr<SendBuffer> CreatePingPacket(const PING& ping, uint16 seq);
		static Sptr<SendBuffer> CreatePongPacket(const PONG& pong, uint16 seq);

		// Ack Packets
		static Sptr<SendBuffer> CreateAckPacket(eAckPacketId id, const void* payload = nullptr, uint32 payloadSize = 0, uint16 seq = 0);

		static Sptr<SendBuffer> CreateReliabilityAckPacket(uint16 seq, uint32 bitfield);

		// Rpc Packets
		static Sptr<SendBuffer> CreateRpcPacket(eRpcPacketId id, uint8 flags = PacketFlags::NONE, const void* payload = nullptr, uint32 payloadSize = 0, uint16 seq = 0);

		// Custom Packets
		static Sptr<SendBuffer> CreateCustomPacket();


		static PacketAnalysis AnalyzePacket(BYTE* buf, uint32 size);

		static bool ParsePacketHeader(BYTE* buf, uint32 size, OUT PacketHeader& pktHeader);
		static uint32 GetRequiredHeaderSize(BYTE* buf, uint32 size);
		static bool IsValidPacket(BYTE* buf, uint32 size);




	private:
		static Sptr<SendBuffer> CreatePacketInternal(uint8 type, uint8 id, uint8 flags, const void* payload, uint32 payloadSize, uint16 seq = 0, uint8 fragIndex = 0, uint8 fragTotal = 0);
	};
}

