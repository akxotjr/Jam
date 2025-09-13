#pragma once


namespace jam::net
{
	struct FragmentHeader;
	struct RpcHeader;
	struct AckHeader;

	enum class eRpcPacketId : uint8;


	enum class ePacketType : uint8
	{
		SYSTEM	= 0,	// 0b 00
		ACK		= 1,	// 0b 01
		RPC		= 2,	// 0b 10
		CUSTOM	= 3		// 0b 11
	};

	enum class eSystemPacketId : uint8
	{
		CONNECT_SYN			= 1,
		CONNECT_SYNACK		= 2,
		CONNECT_ACK			= 3,
		DISCONNECT_FIN		= 4,
		DISCONNECT_FINACK	= 5,
		DISCONNECT_ACK		= 6,

		PING				= 10,
		PONG				= 11,
		HEARTBEAT			= 12,

		SERVER_INFO			= 20,
		CLIENT_INFO			= 21,
	};

	enum class eAckPacketId : uint8
	{
		RELIABILITY_ACK		= 1,
		SELECTIVE_ACK		= 2,
		CUMULATIVE_ACK		= 3,
		FAST_RETRANSMIT		= 4,
		NACK = 5,
	};

#pragma pack(push, 1)
	struct AckHeader
	{
		uint16 latestSeq = 0;
		uint32 bitfield;
	};
#pragma pack(pop)

#pragma pack(push, 1)
	struct NackHeader
	{
		uint16 missingSeq;
		uint32 bitfield;
	};
#pragma pack(pop)

	enum class eChannelType : uint8
	{
		UNRELIABLE_UNORDERED = 0,
		RELIABLE_ORDERED	 = 1,
		UNRELIABLE_SEQUENCED = 2,
		RELIABLE_UNORDERED	 = 3
	};

	constexpr bool HasReliable(eChannelType channel) { return channel == eChannelType::RELIABLE_ORDERED || channel == eChannelType::RELIABLE_UNORDERED; }

	namespace PacketFlags
	{
		constexpr uint8 NONE			= 0x00;		// 0b 0000	
		constexpr uint8 COMPRESSED		= 0x01;		// 0b 0001
		constexpr uint8 ENCRYPTED		= 0x02;		// 0b 0010
		constexpr uint8 FRAGMENTED		= 0x04;		// 0b 0100
		constexpr uint8 PIGGYBACK_ACK	= 0x08;		// 0b 1000
	}

	constexpr bool	HasFlag(uint8 flags, uint8 flag) { return (flags & flag) != 0; }
	constexpr uint8 SetFlag(uint8 flags, uint8 flag) { return flags | flag; }
	constexpr uint8 ClearFlag(uint8 flags, uint8 flag) { return flags & ~flag; }



	// type(2) + id(5) + size(11) + flags(4) + channel(2) = 24bit
#pragma pack(push, 1)
	struct PacketHeader
	{
	public:
		PacketHeader() : data(0), sequence(0), fragmentData(0) {}
		PacketHeader(uint8 type, uint8 id, uint16 size, uint8 flags, uint8 channel, uint16 seq = 0, uint8 fragIndex = 0, uint8 fragTotal = 0)
		{
			data = ((type & MAX_TYPE) << TYPE_SHIFT) |
				   ((id & MAX_ID) << ID_SHIFT) |
				   ((size & MAX_SIZE) << SIZE_SHIFT) |
				   ((flags & MAX_FLAGS) << FLAGS_SHIFT) |
				   ((channel & MAX_CHANNEL) << CHANNEL_SHIFT);
			sequence = seq;
			SetFragmentInfo(fragIndex, fragTotal);
		}

		// Getter
		uint8			GetType() const { return (data & TYPE_MASK) >> TYPE_SHIFT; }				// 2 bit
		uint8			GetId() const { return (data & ID_MASK) >> ID_SHIFT; }				// 6 bit  
		uint16			GetSize() const { return (data & SIZE_MASK) >> SIZE_SHIFT; }				// 11 bit
		uint8			GetFlags() const { return (data & FLAGS_MASK) >> FLAGS_SHIFT; }					// 5 bit
		eChannelType	GetChannel() const { return U2E(eChannelType, (data & CHANNEL_MASK) >> CHANNEL_SHIFT); }
		uint16			GetSequence() const { return sequence; }					// 16 bit
		uint8			GetFragmentIndex() const { return (fragmentData >> 8) & 0xFF; }
		uint8			GetTotalFragments() const { return fragmentData & 0xFF; }

		// Setter
		void			SetType(uint8 type) { data = (data & ~TYPE_MASK) | ((type & MAX_TYPE) << TYPE_SHIFT); }
		void			SetId(uint8 id) { data = (data & ~ID_MASK) | ((id & MAX_ID) << ID_SHIFT); }
		void			SetSize(uint16 size) { data = (data & ~SIZE_MASK) | ((size & MAX_SIZE) << SIZE_SHIFT); }
		void			SetFlags(uint8 flags) { data = (data & ~FLAGS_MASK) | ((flags & MAX_FLAGS) << FLAGS_SHIFT); }
		void			SetChannel(eChannelType channel) { data = (data & ~CHANNEL_MASK) | ((E2U(channel) & MAX_CHANNEL) << CHANNEL_SHIFT); }
		void			SetSequence(uint16 seq) { sequence = seq; }
		void			SetFragmentInfo(uint8 index, uint8 total) { fragmentData = (index << 8) | total; }


		bool			IsReliable() const
		{
			eChannelType channel = GetChannel();
			return channel == eChannelType::RELIABLE_ORDERED || channel == eChannelType::RELIABLE_UNORDERED;
		}
		bool			IsFragmented() const { return HasFlag(GetFlags(), PacketFlags::FRAGMENTED); }
		bool			IsValid() const { return GetType() <= MAX_TYPE && GetId() <= MAX_ID && GetSize() <= MAX_SIZE && GetFlags() <= MAX_FLAGS && E2U(GetChannel()) <= MAX_CHANNEL; }


		static constexpr uint32 GetBaseSize() { return 3; }			// only data : 3 bytes
		static constexpr uint32 GetHalfSize() { return 5; }			// data + sequence : 5 bytes
		static constexpr uint32 GetFullSize() { return 7; }			// data + sequence + fragment : 7 bytes

		uint32			GetActualSize() const { return IsFragmented() ? GetFullSize() : IsReliable() ? GetHalfSize() : GetBaseSize(); }


	private:
		uint32		data : 24;
		uint16		sequence;
		uint16		fragmentData;



	private:
		static constexpr uint32 TYPE_MASK		= 0x00C00000;		// 0b 1100 0000 0000 0000 0000 0000
		static constexpr uint32 ID_MASK			= 0x003E0000;		// 0b 0011 1110 0000 0000 0000 0000  
		static constexpr uint32 SIZE_MASK		= 0x0001FFC0;		// 0b 0000 0001 1111 1111 1100 0000
		static constexpr uint32 FLAGS_MASK		= 0x0000003C;		// 0b 0000 0000 0000 0000 0011 1100
		static constexpr uint32 CHANNEL_MASK	= 0x00000003;		// 0b 0000 0000 0000 0000 0000 0011

		static constexpr uint32 TYPE_SHIFT		= 22;
		static constexpr uint32 ID_SHIFT		= 17;
		static constexpr uint32 SIZE_SHIFT		= 6;
		static constexpr uint32 FLAGS_SHIFT		= 2;
		static constexpr uint32 CHANNEL_SHIFT	= 0;

		static constexpr uint8  MAX_TYPE		= 0x03;				// 2 bit = 4
		static constexpr uint8  MAX_ID			= 0x1F;             // 5 bit = 32
		static constexpr uint16 MAX_SIZE		= 0x7FF;			// 11 bit = 2048
		static constexpr uint8  MAX_FLAGS		= 0x0F;				// 4 bit = 16
		static constexpr uint8  MAX_CHANNEL		= 0x03;				// 2 bit = 4
	};
#pragma pop

	constexpr uint16 ACK_PACKET_SIZE = PacketHeader::GetBaseSize() + sizeof(AckHeader);
	constexpr uint16 NACK_PACKET_SIZE = PacketHeader::GetBaseSize() + sizeof(NackHeader);

	constexpr uint16 MAX_PACKET_SIZE = 1400;
	constexpr uint16 MAX_PAYLOAD_SIZE = MAX_PACKET_SIZE - PacketHeader::GetFullSize() - ACK_PACKET_SIZE;


	struct PacketAnalysis
	{
		bool			IsReliable() const { return header.IsReliable(); }
		bool			IsFragmented() const { return header.IsFragmented(); }
		bool			IsNeedToFragmentation() const { return header.GetSize() > MAX_PACKET_SIZE; }

		ePacketType		GetType() const { return U2E(ePacketType, header.GetType()); }
		uint8			GetId() const { return header.GetId(); }
		uint8			GetFlags() const { return header.GetFlags(); }
		eChannelType	GetChannel() const { return header.GetChannel(); }
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
		static Sptr<SendBuffer> CreatePacket(ePacketType type, uint8 id, uint8 flags = PacketFlags::NONE, eChannelType channel = eChannelType::UNRELIABLE_UNORDERED, const void* payload = nullptr, uint32 payloadSize = 0, uint16 seq = 0, uint8 fragIndex = 0, uint8 fragTotal = 0);

		// System Packets
		static Sptr<SendBuffer> CreateSystemPacket(eSystemPacketId id, uint8 flags = PacketFlags::NONE, eChannelType channel = eChannelType::UNRELIABLE_UNORDERED, const void* payload = nullptr, uint32 payloadSize = 0, uint16 seq = 0);

		static Sptr<SendBuffer> CreateHandshakePacket(eSystemPacketId id);
		static Sptr<SendBuffer> CreatePingPacket(const PING& ping, uint16 seq);
		static Sptr<SendBuffer> CreatePongPacket(const PONG& pong, uint16 seq);

		// Ack Packets
		static Sptr<SendBuffer> CreateAckPacket(eAckPacketId id, const void* payload = nullptr, uint32 payloadSize = 0, uint16 seq = 0);

		static Sptr<SendBuffer> CreateReliabilityAckPacket(uint16 seq, uint32 bitfield);
		static Sptr<SendBuffer> CreateNackPacket(uint16 seq, uint32 bitfield);
		// Rpc Packets
		static Sptr<SendBuffer> CreateRpcPacket(eRpcPacketId id, uint8 flags = PacketFlags::NONE, uint8 channel, const void* payload = nullptr, uint32 payloadSize = 0, uint16 seq = 0);

		// Custom Packets
		static Sptr<SendBuffer> CreateCustomPacket();


		static PacketAnalysis	AnalyzePacket(BYTE* buf, uint32 size);

		static bool				ParsePacketHeader(BYTE* buf, uint32 size, OUT PacketHeader& pktHeader);
		static uint32			GetRequiredHeaderSize(BYTE* buf, uint32 size);
		static bool				IsValidPacket(BYTE* buf, uint32 size);




	private:
		static Sptr<SendBuffer> CreatePacketInternal(uint8 type, uint8 id, uint8 flags, uint8 channel, const void* payload, uint32 payloadSize, uint16 seq = 0, uint8 fragIndex = 0, uint8 fragTotal = 0);
	};
}

