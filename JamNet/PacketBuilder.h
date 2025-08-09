#pragma once
#include "BufferReader.h"
#include "BufferWriter.h"
#include "CongestionController.h"

namespace jam::net
{
	struct PacketHeader;
	struct RudpHeader;
	struct FragmentHeader;
	struct SysHeader;
	struct RpcHeader;
	struct AckHeader;


	enum class ePacketMode : uint8
	{
		SMALL_PKT,
		GAME_PKT,
		LARGE_PKT,
	};


	struct GamePacketLayout
	{
		static constexpr uint32 PACKET_HEADER_OFFSET = 0;                   // 0
		static constexpr uint32 RUDP_HEADER_OFFSET = 3;                     // 3  (if reliable)
		static constexpr uint32 FRAGMENT_HEADER_OFFSET = 5;                 // 5  (if fragment)

		// RPC & CUSTOM : mutually exclusive
		static constexpr uint32 RPC_HEADER_OFFSET = 15;                     // 15 (or 5 if no fragment)
		static constexpr uint32 CUSTOM_HEADER_OFFSET = 15;

		static constexpr uint32 PAYLOAD_OFFSET = 22;                        // 22 

		static constexpr uint32 MAX_HEADER_SIZE = PAYLOAD_OFFSET;
	};


	enum class eGamePacketFlags : uint8
	{
		NONE = 0,
		HAS_RUDP_HEADER = 1 << 0,
		HAS_FRAGMENT_HEADER = 1 << 1,
		HAS_RPC_HEADER = 1 << 2,
		HAS_CUSTOM_HEADER = 1 << 3,
	};
	DEFINE_ENUM_FLAG_OPERATORS(eGamePacketFlags)


	class PacketBuilder
	{
	public:
		PacketBuilder() = default;
		~PacketBuilder() = default;

		void BeginWrite(ePacketMode mode, uint32 allocSize = 0);
		void EndWrite();

		template<typename... Headers>
		void AttachHeaders(const Headers&... headers)
		{
			(m_bufferWriter->Write(headers), ...);
		}
		void AttachPayload(const void* data, uint32 size) { m_bufferWriter->WriteBytes(data, size); }

		void BeginRead(BYTE* buffer, uint32 size);
		void EndRead();

		template<typename... Headers>
		void DetachHeaders(OUT Headers&... headers)
		{
			(m_bufferReader->Read(headers), ...);
		}
		void DetachPayload(OUT void* data, uint32 size) { m_bufferReader->ReadBytes(data, size); }

		//void Finalize() { m_bufferWriter->Close(); }

		Sptr<SendBuffer> GetSendBuffer() const { return m_sendBuffer; }


	private:
		Sptr<SendBuffer>	m_sendBuffer;
		Uptr<BufferWriter>	m_bufferWriter;
		Uptr<BufferReader>	m_bufferReader;
		ePacketMode			m_mode;
	};
}

