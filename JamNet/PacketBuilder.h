#pragma once
#include "BufferReader.h"

namespace jam::net
{
	class BufferReader;
	struct FragmentHeader;

	class PacketBuilder
	{
	public:
		PacketBuilder() = default;
		~PacketBuilder() = default;

		void BeginWrite(uint32 allocSize = MTU);
		void EndWrite();

		void AttachPacketHeader(const PacketHeader& header) { m_bufferWriter->Write(header); }
		void AttachRudpheader(const RudpHeader& header) { m_bufferWriter->Write(header); }
		void AttachFragmentHeader(const FragmentHeader& header) { m_bufferWriter->Write(header); }
		void AttachSysHeader(const SysHeader& header) { m_bufferWriter->Write(header); }
		void AttachRpcHeader(const RpcHeader& header) { m_bufferWriter->Write(header); }
		void AttachAckHeader(const AckHeader& header) { m_bufferWriter->Write(header); }
		//void AttachCustomHeader();
		void AttachPayload(const void* data, uint32 size) { m_bufferWriter->WriteBytes(data, size); }

		void BeginRead();
		void EndRead();

		void DetachPacketHeader(OUT PacketHeader& header) { m_bufferReader->Read(header); }
		void DetachRudpheader(OUT RudpHeader& header) { m_bufferReader->Read(header); }
		void DetachFragmentHeader(OUT FragmentHeader& header) { m_bufferReader->Read(header); }
		void DetachSysHeader(OUT SysHeader& header) { m_bufferReader->Read(header); }
		void DetachRpcHeader(OUT RpcHeader& header) { m_bufferReader->Read(header); }
		void DetachAckHeader(OUT AckHeader& header) { m_bufferReader->Read(header); }
		//void DetachCustomHeader();
		void DetachPayload(OUT void* data, uint32 size) { m_bufferReader->ReadBytes(data, size); }

		void Finalize() { m_bufferWriter->Close(); };

	private:
		Sptr<SendBuffer>	m_sendBuffer;

		Uptr<BufferWriter>	m_bufferWriter;
		Uptr<BufferReader>	m_bufferReader;
	};
}

