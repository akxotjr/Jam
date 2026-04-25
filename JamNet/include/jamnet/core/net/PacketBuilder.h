#pragma once
#include "jamnet/core/net/Buffer.h"
#include "jamnet/core/net/PacketStructure.h"


namespace jam::net
{

	struct RpcHeader;


	enum class eRpcPacketId : uint8;


	// fixed[3] = type(2) + id(5) + size(11) + flags(4) + channel(2) = 24bit
	#pragma pack(push, 1)
	struct PacketHeader
	{
	public:
		PacketHeader() = default;

		PacketHeader(uint8 type, uint8 id, uint16 size, uint8 flags, uint8 channel, uint16 packetSeq = 0, uint16 orderedSeq = 0, uint8 fragIndex = 0, uint8 fragTotal = 0)
			: packetSeq(packetSeq), orderedSeq(orderedSeq), fragInfo(PackFragInfo(fragIndex, fragTotal))
		{
			uint32 bits = 0;
			bits |= (static_cast<uint32>(type	 & MAX_TYPE)	<< TYPE_SHIFT);
			bits |= (static_cast<uint32>(id		 & MAX_ID)		<< ID_SHIFT);
			bits |= (static_cast<uint32>(size    & MAX_PACKET_SIZE) << SIZE_SHIFT);
			bits |= (static_cast<uint32>(flags   & MAX_FLAGS)	<< FLAGS_SHIFT);
			bits |= (static_cast<uint32>(channel & MAX_CHANNEL) << CHANNEL_SHIFT);
			SetFixedBits(bits);
		}

		// Getter
		uint8			GetType()			 const { return static_cast<uint8>((GetFixedBits() & TYPE_MASK) >> TYPE_SHIFT); }				// 2 bit
		uint8			GetId()				 const { return static_cast<uint8>((GetFixedBits() & ID_MASK) >> ID_SHIFT); }						// 5 bit  
		uint16			GetSize()			 const { return static_cast<uint16>((GetFixedBits() & SIZE_MASK) >> SIZE_SHIFT); }				// 11 bit
		uint8			GetFlags()			 const { return static_cast<uint8>((GetFixedBits() & FLAGS_MASK) >> FLAGS_SHIFT); }				// 4 bit
		uint8			GetChannel()		 const { return static_cast<uint8>((GetFixedBits() & CHANNEL_MASK) >> CHANNEL_SHIFT); }
		uint16			GetSequence()		 const { return packetSeq; }
		uint16			GetOrderedSequence() const { return orderedSeq; }
		uint16			GetFragmentInfo()	 const { return fragInfo; }
		uint8			GetFragmentIndex()	 const { return static_cast<uint8>(fragInfo & 0x00FFu); }
		uint8			GetTotalFragments()  const { return static_cast<uint8>((fragInfo & 0xFF00u) >> 8); }

		ePacketGroup	GetGroup() const { return U2E(ePacketGroup, (GetFixedBits() & GROUP_MASK) >> GROUP_SHIFT); }

		void SetType(uint8 type)
		{
			uint32 bits = GetFixedBits();
			bits = (bits & ~TYPE_MASK) | (static_cast<uint32>(type & MAX_TYPE) << TYPE_SHIFT);
			SetFixedBits(bits);
		}

		void SetId(uint8 id)
		{
			uint32 bits = GetFixedBits();
			bits = (bits & ~ID_MASK) | (static_cast<uint32>(id & MAX_ID) << ID_SHIFT);
			SetFixedBits(bits);
		}

		void SetSize(uint16 size)
		{
			uint32 bits = GetFixedBits();
			bits = (bits & ~SIZE_MASK) | (static_cast<uint32>(size & MAX_PACKET_SIZE) << SIZE_SHIFT);
			SetFixedBits(bits);
		}

		void SetFlags(uint8 flags)
		{
			uint32 bits = GetFixedBits();
			bits = (bits & ~FLAGS_MASK) | (static_cast<uint32>(flags & MAX_FLAGS) << FLAGS_SHIFT);
			SetFixedBits(bits);
		}

		void SetChannel(eChannel channel)
		{
			uint32 bits = GetFixedBits();
			bits = (bits & ~CHANNEL_MASK) | (static_cast<uint32>(E2U(channel) & MAX_CHANNEL) << CHANNEL_SHIFT);
			SetFixedBits(bits);
		}

		void SetSequence(uint16 seq)
		{
			packetSeq = seq;
		}

		void SetOrderedSequence(uint16 seq)
		{
			orderedSeq = seq;
		}

		void SetFragmentInfo(uint8 index, uint8 total)
		{
			fragInfo = PackFragInfo(index, total);
		}

		bool IsReliable() const
		{
			uint8 ch = GetChannel();
			return ch == E2U(eChannel::RELIABLE_ORDERED) || ch == E2U(eChannel::RELIABLE_UNORDERED);
		}

		bool IsFragmented() const { return HasFlag(GetFlags(), PacketFlags::FRAGMENTED); }

		bool IsValid() const
		{
			return (GetType() <= MAX_TYPE) && (GetId() <= MAX_ID) && (GetSize() >= BASE_SIZE) && (GetSize() <= MAX_PACKET_SIZE) && (GetFlags() <= MAX_FLAGS) && (GetChannel() <= MAX_CHANNEL);
		}

		uint32	GetActualSize() const
		{
			return CalcHeaderSize(U2E(eChannel, GetChannel()), GetFlags());
		}

		static constexpr uint32 BASE_SIZE = 3;
		static constexpr uint32 HALF_SIZE = 5;
		static constexpr uint32 FULL_SIZE = 7;
		static constexpr uint32 MAX_WIRE_SIZE = 9;
		static constexpr uint16 MAX_PACKET_SIZE = 0x7FF;

		static uint32 CalcHeaderSize(eChannel channel, uint8 flags)
		{
			if (!HasSequence(channel))
				return BASE_SIZE;

			uint32 size = HALF_SIZE; // fixed + packetSeq
			if (HasOrderedSequence(channel))
				size += sizeof(uint16);
			if (HasFlag(flags, PacketFlags::FRAGMENTED))
				size += sizeof(uint16);

			return size;
		}


	private:
		static constexpr uint16 PackFragInfo(uint8 index, uint8 total)
		{
			return static_cast<uint16>(index) | (static_cast<uint16>(total) << 8);
		}

		uint32 GetFixedBits() const
		{
			return static_cast<uint32>(fixed[0])
				| (static_cast<uint32>(fixed[1]) << 8)
				| (static_cast<uint32>(fixed[2]) << 16);
		}

		void SetFixedBits(uint32 bits)
		{
			fixed[0] = static_cast<BYTE>(bits & 0xFFu);
			fixed[1] = static_cast<BYTE>((bits >> 8) & 0xFFu);
			fixed[2] = static_cast<BYTE>((bits >> 16) & 0xFFu);
		}

		BYTE		fixed[BASE_SIZE] = {};
		uint16		packetSeq = 0;
		uint16		orderedSeq = 0;
		uint16		fragInfo = 0;

		static constexpr uint32 GROUP_MASK		 = 0x00000002u;  // bit [1] (type의 상위 비트)
		static constexpr uint32 TYPE_MASK		 = 0x00000003u;  // bits [0-1]   (2비트)
		static constexpr uint32 ID_MASK			 = 0x0000007Cu;  // bits [2-6]   (5비트)
		static constexpr uint32 SIZE_MASK		 = 0x0003FF80u;  // bits [7-17]  (11비트)
		static constexpr uint32 FLAGS_MASK		 = 0x003C0000u;  // bits [18-21] (4비트)
		static constexpr uint32 CHANNEL_MASK	 = 0x00C00000u;  // bits [22-23] (2비트)

		static constexpr uint32 GROUP_SHIFT		 = 1;   // type의 상위 비트
		static constexpr uint32 TYPE_SHIFT		 = 0;
		static constexpr uint32 ID_SHIFT		 = 2;
		static constexpr uint32 SIZE_SHIFT		 = 7;
		static constexpr uint32 FLAGS_SHIFT		 = 18;
		static constexpr uint32 CHANNEL_SHIFT	 = 22;

		static constexpr uint8  MAX_TYPE				= 0x03;   // 2비트
		static constexpr uint8	MAX_GROUP				= 0x01;
		static constexpr uint8  MAX_ID					= 0x1F;   // 5비트
		static constexpr uint8  MAX_FLAGS				= 0x0F;   // 4비트
		static constexpr uint8  MAX_CHANNEL				= 0x03;   // 2비트
	};
	#pragma pack(pop)

	static_assert(sizeof(PacketHeader) == PacketHeader::MAX_WIRE_SIZE, "PacketHeader wire layout must remain packed to 9 bytes");

	// 개별 ACK 패킷 크기 (BaseHeader + ACK_DATA)
	constexpr uint16 ACK_PACKET_SIZE  = PacketHeader::BASE_SIZE + sizeof(ACK_DATA);
	constexpr uint16 NACK_PACKET_SIZE = PacketHeader::BASE_SIZE + sizeof(NACK_DATA);

	// 최대 페이로드 크기(보수): 최대 헤더 + Piggyback ACK 데이터 공간을 고려
	constexpr uint16 MAX_PAYLOAD_SIZE = JAMNET_MTU - PacketHeader::MAX_WIRE_SIZE - sizeof(ACK_DATA);



	struct PacketHeaderView
	{
		bool            isValid		= false;
		BYTE*			data		= nullptr;
		PacketHeader	header		= {};
		BYTE*			payload		= nullptr;
		uint32          headerSize  = 0;
		uint32          payloadSize = 0;
		uint32          totalSize   = 0;


		PacketHeaderView() = default;

		static PacketHeaderView Parse(BYTE* buf, uint32 size)
		{
			PacketHeaderView view{};

			if (!buf || size < PacketHeader::BASE_SIZE)
				return view;  // isValid = false

			view.data   = buf;
			std::memcpy(&view.header, buf, PacketHeader::BASE_SIZE);

			if (!view.header.IsValid())
				return view;

			view.headerSize = view.header.GetActualSize();

			if (size < view.headerSize)
				return view;

			std::memcpy(&view.header, buf, view.headerSize);
			view.totalSize = view.header.GetSize();

			if (view.totalSize < view.headerSize || size < view.totalSize)
			{
				JAMNET_LOG_WARN("Invalid packet size. headerSize={}, totalSize={}, available={}", view.headerSize, view.totalSize, size);
				return view;
			}

			view.payloadSize = view.totalSize - view.headerSize;
			view.payload     = buf + view.headerSize;
			view.isValid     = true;

			return view;
		}

		bool			IsValid()				const { return isValid; }
		bool			IsReliable()			const { return isValid && header.IsReliable(); }
		bool			IsFragmented()			const { return isValid && header.IsFragmented(); }
		bool			IsNeedToFragmentation() const { return isValid && header.GetGroup() == ePacketGroup::NORMAL && header.IsReliable() && header.GetSize() > JAMNET_MTU; }
		bool			IsCtrlGroup()			const { return isValid && header.GetGroup() == ePacketGroup::CTRL; }
		bool			IsNormalGroup()			const { return isValid && header.GetGroup() == ePacketGroup::NORMAL; }

		ePacketGroup    Group()			  const { return header.GetGroup(); }
		ePacketType     Type()			  const { return U2E(ePacketType, header.GetType()); }
		uint8           Id()			  const { return header.GetId(); }
		uint8           Flags()			  const { return header.GetFlags(); }
		eChannel		Channel()		  const { return U2E(eChannel, header.GetChannel()); }
		uint16          Sequence()		  const { return header.GetSequence(); }
		uint16			OrderedSequence() const { return header.GetOrderedSequence(); }
		uint8           FragmentIndex()   const { return header.GetFragmentIndex(); }
		uint8           TotalFragments()  const { return header.GetTotalFragments(); }


		PacketHeader*		Header()		  { return &header; }
		const PacketHeader*	Header()	const { return &header; }
		void				WriteHeaderTo(BYTE* dst) const
		{
			JAM_ASSERT(isValid);
			JAM_ASSERT(dst != nullptr);
			std::memcpy(dst, &header, headerSize);
		}
		void				WriteHeaderTo(Packet& packet) const
		{
			JAM_ASSERT(packet.IsValid());
			WriteHeaderTo(packet->Head());
		}
		uint32			HeaderSize()  const { return headerSize; }
		BYTE*			Payload()	  const { return payload; }
		uint32          PayloadSize() const { return payloadSize; }
		uint32          TotalSize()   const { return totalSize; }
	};


	class PacketBuilder
	{
	public:
		// Common
		static Packet		CreatePacket(ePacketType type, uint8 id, uint8 flags = PacketFlags::NONE, eChannel channel = eChannel::UNRELIABLE_UNORDERED, const void* payload = nullptr, uint32 payloadSize = 0, uint16 packetSeq = 0, uint16 orderdSeq = 0, uint8 fragIndex = 0, uint8 fragTotal = 0);

		// System 
		static Packet		CreateSystemPacket(eSystemPacketId id, uint8 flags = PacketFlags::NONE, eChannel channel = eChannel::UNRELIABLE_UNORDERED, const void* payload = nullptr, uint32 payloadSize = 0);
		static Packet		CreateHandshakePacket(eSystemPacketId id);
		static Packet		CreatePingPacket(const PING_DATA& ping);
		static Packet		CreatePongPacket(const PONG_DATA& pong);

		// Ack 
		static Packet		CreateAckPacket(const ACK_DATA& ack);
		static Packet		CreateNackPacket(const NACK_DATA& nack);

		// Rpc
		static Packet		CreateRpcPacket(const RpcHeader* rpc, const void* payload = nullptr, uint32 payloadSize = 0, uint8 flags = PacketFlags::NONE, eChannel ch = eChannel::RELIABLE_ORDERED);

		// Custom
		static Packet		CreateCustomPacket(uint8 id, uint8 flags, eChannel channel, const void* payload, uint32 payloadSize);


	private:
		static Packet		CreatePacketInternal(uint8 type, uint8 id, uint8 flags, uint8 ch, const void* payload, uint32 payloadSize, uint16 packetSeq = 0, uint16 orderdSeq = 0, uint8 fragIndex = 0, uint8 fragTotal = 0);
	};
}

