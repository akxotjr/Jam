#pragma once
#include "BufferReader.h"
#include "BufferWriter.h"
#include "PacketStructure.h"
#include "SendBuffer.h"


namespace jam::net
{

	struct RpcHeader;


	enum class eRpcPacketId : uint8;


	// type(2) + id(5) + size(11) + flags(4) + channel(2) = 24bit
#pragma pack(push, 1)
	struct PacketHeader
	{
	public:
		PacketHeader() = default;

		PacketHeader(uint8 type, uint8 id, uint16 size, uint8 flags, uint8 channel, uint16 seq = 0, uint8 fragIndex = 0, uint8 fragTotal = 0)
		{
			data |= (static_cast<uint64>(type & MAX_TYPE) << TYPE_SHIFT);
			data |= (static_cast<uint64>(id & MAX_ID) << ID_SHIFT);
			data |= (static_cast<uint64>(size & MAX_SIZE) << SIZE_SHIFT);
			data |= (static_cast<uint64>(flags & MAX_FLAGS) << FLAGS_SHIFT);
			data |= (static_cast<uint64>(channel & MAX_CHANNEL) << CHANNEL_SHIFT);

			// sequence (16비트, optional)
			data |= (static_cast<uint64>(seq) << SEQUENCE_SHIFT);

			// fragment (16비트 = 8비트 index + 8비트 total, optional)
			data |= (static_cast<uint64>(fragIndex) << FRAG_INDEX_SHIFT);
			data |= (static_cast<uint64>(fragTotal) << FRAG_TOTAL_SHIFT);
		}

		// Getter
		uint8			GetType() const { return static_cast<uint8>((data & TYPE_MASK) >> TYPE_SHIFT); }				// 2 bit
		uint8			GetId() const { return static_cast<uint8>((data & ID_MASK) >> ID_SHIFT); }						// 6 bit  
		uint16			GetSize() const { return static_cast<uint16>((data & SIZE_MASK) >> SIZE_SHIFT); }				// 11 bit
		uint8			GetFlags() const { return static_cast<uint8>((data & FLAGS_MASK) >> FLAGS_SHIFT); }				// 5 bit
		uint8			GetChannel() const { return static_cast<uint8>((data & CHANNEL_MASK) >> CHANNEL_SHIFT); }
		uint16			GetSequence() const { return static_cast<uint16>((data & SEQUENCE_MASK) >> SEQUENCE_SHIFT); }
		uint8			GetFragmentIndex() const { return static_cast<uint8>((data & FRAG_INDEX_MASK) >> FRAG_INDEX_SHIFT); }
		uint8			GetTotalFragments() const { return static_cast<uint8>((data & FRAG_TOTAL_MASK) >> FRAG_TOTAL_SHIFT); }

		ePacketGroup	GetGroup() const { return U2E(ePacketGroup, (data & GROUP_MASK) >> GROUP_SHIFT); }


		void SetType(uint8 type)
		{
			data = (data & ~TYPE_MASK) | (static_cast<uint64>(type & MAX_TYPE) << TYPE_SHIFT);
		}

		void SetId(uint8 id)
		{
			data = (data & ~ID_MASK) | (static_cast<uint64>(id & MAX_ID) << ID_SHIFT);
		}

		void SetSize(uint16 size)
		{
			data = (data & ~SIZE_MASK) | (static_cast<uint64>(size & MAX_SIZE) << SIZE_SHIFT);
		}

		void SetFlags(uint8 flags)
		{
			data = (data & ~FLAGS_MASK) | (static_cast<uint64>(flags & MAX_FLAGS) << FLAGS_SHIFT);
		}

		void SetChannel(eChannelType channel)
		{
			data = (data & ~CHANNEL_MASK) | (static_cast<uint64>(E2U(channel) & MAX_CHANNEL) << CHANNEL_SHIFT);
		}

		void SetSequence(uint16 seq)
		{
			data = (data & ~SEQUENCE_MASK) | (static_cast<uint64>(seq) << SEQUENCE_SHIFT);
		}

		void SetFragmentInfo(uint8 index, uint8 total)
		{
			data = (data & ~(FRAG_INDEX_MASK | FRAG_TOTAL_MASK)) |
				(static_cast<uint64>(index) << FRAG_INDEX_SHIFT) |
				(static_cast<uint64>(total) << FRAG_TOTAL_SHIFT);
		}

		bool IsReliable() const
		{
			uint8 ch = GetChannel();
			return ch == E2U(eChannelType::RELIABLE_ORDERED) || ch == E2U(eChannelType::RELIABLE_UNORDERED);
		}

		bool IsFragmented() const { return HasFlag(GetFlags(), PacketFlags::FRAGMENTED); }

		bool IsValid() const
		{
			return (GetType() <= MAX_TYPE) && (GetId() <= MAX_ID) && (GetSize() <= MAX_SIZE) && (GetFlags() <= MAX_FLAGS) && (GetChannel() <= MAX_CHANNEL);
		}

		uint32	GetActualSize() const
		{
			const auto ch = U2E(eChannelType, GetChannel());
			return IsFragmented() ? FULL_SIZE : HasSequence(ch) ? HALF_SIZE : BASE_SIZE;
		}

		static constexpr uint32 BASE_SIZE = 3;
		static constexpr uint32 HALF_SIZE = 5;
		static constexpr uint32 FULL_SIZE = 7;


	private:
		uint64		data = 0;

		static constexpr uint64 GROUP_MASK		 = 0x0000000000000002ULL;  // bit [1] (type의 상위 비트)
		static constexpr uint64 TYPE_MASK		 = 0x0000000000000003ULL;  // bits [0-1]   (2비트)
		static constexpr uint64 ID_MASK			 = 0x000000000000007CULL;  // bits [2-6]   (5비트)
		static constexpr uint64 SIZE_MASK		 = 0x000000000003FF80ULL;  // bits [7-17]  (11비트)
		static constexpr uint64 FLAGS_MASK		 = 0x00000000003C0000ULL;  // bits [18-21] (4비트)
		static constexpr uint64 CHANNEL_MASK	 = 0x0000000000C00000ULL;  // bits [22-23] (2비트)
		static constexpr uint64 SEQUENCE_MASK	 = 0x000000FFFF000000ULL;  // bits [24-39] (16비트)
		static constexpr uint64 FRAG_INDEX_MASK  = 0x0000FF0000000000ULL;  // bits [40-47] (8비트)
		static constexpr uint64 FRAG_TOTAL_MASK  = 0x00FF000000000000ULL;

		static constexpr uint32 GROUP_SHIFT		 = 1;   // type의 상위 비트
		static constexpr uint32 TYPE_SHIFT		 = 0;
		static constexpr uint32 ID_SHIFT		 = 2;
		static constexpr uint32 SIZE_SHIFT		 = 7;
		static constexpr uint32 FLAGS_SHIFT		 = 18;
		static constexpr uint32 CHANNEL_SHIFT	 = 22;
		static constexpr uint32 SEQUENCE_SHIFT	 = 24;
		static constexpr uint32 FRAG_INDEX_SHIFT = 40;
		static constexpr uint32 FRAG_TOTAL_SHIFT = 48;

		static constexpr uint8  MAX_TYPE		 = 0x03;   // 2비트
		static constexpr uint8	MAX_GROUP		 = 0x01;
		static constexpr uint8  MAX_ID			 = 0x1F;   // 5비트
		static constexpr uint16 MAX_SIZE		 = 0x7FF;  // 11비트
		static constexpr uint8  MAX_FLAGS		 = 0x0F;   // 4비트
		static constexpr uint8  MAX_CHANNEL		 = 0x03;   // 2비트
	};
#pragma pack(pop)

	// 개별 ACK 패킷 크기 (BaseHeader + ACK_DATA)
	constexpr uint16 ACK_PACKET_SIZE  = PacketHeader::BASE_SIZE + sizeof(ACK_DATA);
	constexpr uint16 NACK_PACKET_SIZE = PacketHeader::BASE_SIZE + sizeof(NACK_DATA);

	// 최대 페이로드 크기(보수): 최대 헤더(Full) + Piggyback ACK 데이터 공간을 고려
	constexpr uint16 MAX_PAYLOAD_SIZE = JAMNET_MTU - PacketHeader::FULL_SIZE - sizeof(ACK_DATA);



	struct PacketView
	{
		bool            isValid		= false;
		BYTE*			data		= nullptr;
		PacketHeader*	header		= nullptr;
		BYTE*			payload		= nullptr;
		uint32          headerSize  = 0;
		uint32          payloadSize = 0;
		uint32          totalSize   = 0;



		// 기본 생성자 (무효한 상태)
		PacketView() = default;

		// 버퍼에서 직접 파싱 (AnalyzePacket 로직 통합)
		static PacketView Parse(BYTE* buf, uint32 size)
		{
			PacketView view{};

			if (size < PacketHeader::BASE_SIZE)
				return view;  // isValid = false

			BufferReader br(buf, size);

			// 1. Base 헤더 읽기
			view.data = buf;
			view.header = reinterpret_cast<PacketHeader*>(buf);

			if (!view.header->IsValid())
				return view;

			// 2. 실제 헤더 크기 확인
			view.headerSize = view.header->GetActualSize();

			if (size < view.headerSize)
				return view;

			// 3. 전체 패킷 크기 검증
			view.totalSize = view.header->GetSize();

			if (size < view.totalSize || view.totalSize < view.headerSize)
			{
				JAMNET_LOG_CRITICAL("view.headerSize= {}, view.totalSize= {} size= {}", view.headerSize, view.totalSize, size);
				return view;
			}
			// 4. 페이로드 설정
			view.payloadSize = view.totalSize - view.headerSize;
			view.payload = buf + view.headerSize;

			view.isValid = true;
			return view;
		}

		bool			IsValid() const { return isValid; }
		bool			IsReliable() const { return header && header->IsReliable(); }
		bool			IsFragmented() const { return header && header->IsFragmented(); }
		bool			IsNeedToFragmentation() const { return header && header->GetGroup() == ePacketGroup::NORMAL && header->IsReliable() && header->GetSize() > JAMNET_MTU; }
		bool			IsCtrlGroup() const { return header && header->GetGroup() == ePacketGroup::CTRL; }
		bool			IsNormalGroup() const { return header && header->GetGroup() == ePacketGroup::NORMAL; }

		ePacketGroup    Group() const { return header->GetGroup(); }
		ePacketType     Type() const { return U2E(ePacketType, header->GetType()); }
		uint8           Id() const { return header->GetId(); }
		uint8           Flags() const { return header->GetFlags(); }
		eChannelType    Channel() const { return U2E(eChannelType, header->GetChannel()); }
		uint16          Sequence() const { return header->GetSequence(); }
		uint8           FragmentIndex() const { return header->GetFragmentIndex(); }
		uint8           TotalFragments() const { return header->GetTotalFragments(); }

		PacketHeader*	Header() const { return header; }
		uint32			HeaderSize() const { return headerSize; }
		BYTE*			Payload() const { return payload; }
		uint32          PayloadSize() const { return payloadSize; }
		uint32          TotalSize() const { return totalSize; }
	};


	struct RpcPacketOpenResult
	{
		shared_ptr<SendBuffer>		buf;     // 생성된 송신 버퍼
		BufferWriter				writer;  // PacketHeader 후부터 쓰기 시작하도록 설정됨
		uint32						headerSize = 0; // PacketHeader 실제 크기
		uint32						totalSize = 0; // headerSize + payloadSize

		bool						IsValid() const { return static_cast<bool>(buf); }
	};


	class PacketBuilder
	{
	public:
		// Common
		static shared_ptr<SendBuffer>		CreatePacket(ePacketType type, uint8 id, uint8 flags = PacketFlags::NONE, eChannelType channel = eChannelType::UNRELIABLE_UNORDERED, const void* payload = nullptr, uint32 payloadSize = 0, uint16 seq = 0, uint8 fragIndex = 0, uint8 fragTotal = 0);

		// System 
		static shared_ptr<SendBuffer>		CreateSystemPacket(eSystemPacketId id, uint8 flags = PacketFlags::NONE, eChannelType channel = eChannelType::UNRELIABLE_UNORDERED, const void* payload = nullptr, uint32 payloadSize = 0);
		static shared_ptr<SendBuffer>		CreateHandshakePacket(eSystemPacketId id);
		static shared_ptr<SendBuffer>		CreatePingPacket(const PING_DATA& ping);
		static shared_ptr<SendBuffer>		CreatePongPacket(const PONG_DATA& pong);

		// Ack 
		static shared_ptr<SendBuffer>		CreateAckPacket(const ACK_DATA& ack);
		static shared_ptr<SendBuffer>		CreateNackPacket(const NACK_DATA& nack);

		// Rpc
		static shared_ptr<SendBuffer>		CreateRpcPacket(eRpcPacketId id, uint8 flags = PacketFlags::NONE, eChannelType channel = eChannelType::RELIABLE_ORDERED, const void* payload = nullptr, uint32 payloadSize = 0);
		static RpcPacketOpenResult			OpenRpcPacket(eRpcPacketId id, uint8 flags, eChannelType channel, uint32 payloadSize);

		// Custom
		static shared_ptr<SendBuffer>		CreateCustomPacket(uint8 id, uint8 flags, eChannelType channel, const void* payload, uint32 payloadSize);


	private:
		static shared_ptr<SendBuffer>		CreatePacketInternal(uint8 type, uint8 id, uint8 flags, uint8 channel, const void* payload, uint32 payloadSize, uint16 seq = 0, uint8 fragIndex = 0, uint8 fragTotal = 0);
	};
}

