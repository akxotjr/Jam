#pragma once
#include "BufferReader.h"
#include "BufferWriter.h"

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
		PacketBuilder() = default;
		~PacketBuilder() = default;

		void BeginWrite(uint32 allocSize = MTU);
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

		void Finalize() { m_bufferWriter->Close(); }

		Sptr<SendBuffer> GetSendBuffer() const { return m_sendBuffer; }

	private:
		Sptr<SendBuffer>	m_sendBuffer;
		Uptr<BufferWriter>	m_bufferWriter;
		Uptr<BufferReader>	m_bufferReader;
	};
}

