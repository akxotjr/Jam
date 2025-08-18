#include "pch.h"
#include "PacketBuilder.h"
#include "BufferReader.h"


namespace jam::net
{
	Sptr<SendBuffer> PacketBuilder::CreatePacket(ePacketType type, uint8 id, uint8 flags, const void* payload, uint32 payloadSize, uint16 seq, uint8 fragIndex, uint8 fragTotal)
	{
		return CreatePacketInternal(E2U(type), id, flags, payload, payloadSize, seq, fragIndex, fragTotal);
	}

	Sptr<SendBuffer> PacketBuilder::CreateSystemPacket(eSystemPacketId id, uint8 flags, const void* payload, uint32 payloadSize, uint16 seq)
	{
		return CreatePacketInternal(E2U(ePacketType::SYSTEM), E2U(id), flags, payload, payloadSize, seq);
	}

	Sptr<SendBuffer> PacketBuilder::CreateHandshakePacket(eSystemPacketId id)
	{
		return CreateSystemPacket(id, PacketFlags::NONE, nullptr, 0);
	}

	Sptr<SendBuffer> PacketBuilder::CreatePingPacket(const PING& ping, uint16 seq)
	{
		return CreateSystemPacket(eSystemPacketId::PING, PacketFlags::RELIABLE, &ping, sizeof(ping), seq);
	}

	Sptr<SendBuffer> PacketBuilder::CreatePongPacket(const PONG& pong, uint16 seq)
	{
		return CreateSystemPacket(eSystemPacketId::PING, PacketFlags::RELIABLE, &pong, sizeof(pong), seq);
	}

	Sptr<SendBuffer> PacketBuilder::CreateReliabilityAckPacket(uint16 seq, uint32 bitfield)
	{
		constexpr uint32 size = sizeof(PacketHeader) + sizeof(AckHeader);
		Sptr<SendBuffer> buf = SendBufferManager::Instance().Open(size);
		BufferWriter bw(buf->Buffer(), buf->AllocSize());



		buf->Close(bw.WriteSize());

		return buf;
	}

	Sptr<SendBuffer> PacketBuilder::CreateRpcPacket(eRpcPacketId id, uint8 flags, const void* payload, uint32 payloadSize, uint16 seq)
	{
		return CreatePacketInternal(E2U(ePacketType::RPC), E2U(id), flags, payload, payloadSize, seq);
	}


	PacketAnalysis PacketBuilder::AnalyzePacket(BYTE* buf, uint32 size)
	{
		PacketAnalysis result = {};

		if (size < PacketHeader::GetBaseSize())
			return result;

		BufferReader br(buf, size);

		if (!br.PeekBytes(&result.header, PacketHeader::GetBaseSize()))
			return result;

		bool isReliable = result.header.IsReliable();
		result.headerSize = isReliable ? PacketHeader::GetHalfSize() : PacketHeader::GetBaseSize();

		if (size < result.headerSize)
			return result;

		if (isReliable)
		{
			if (!br.ReadBytes(&result.header, PacketHeader::GetHalfSize()))
				return result;
		}
		else
		{
			if (!br.ReadBytes(&result.header, PacketHeader::GetBaseSize()))
				return result;
		}

		result.totalSize = result.header.GetSize();
		if (size < result.totalSize || result.totalSize < result.headerSize)
			return result;

		result.payloadSize = result.totalSize - result.headerSize;

		result.isValid = true;
		return result;
	}

	bool PacketBuilder::ParsePacketHeader(BYTE* buf, uint32 size, PacketHeader& pktHeader)
	{
		PacketAnalysis analysis = AnalyzePacket(buf, size);
		if (analysis.isValid)
		{
			pktHeader = analysis.header;
			return true;
		}
		return false;
	}

	uint32 PacketBuilder::GetRequiredHeaderSize(BYTE* buf, uint32 size)
	{
		PacketAnalysis analysis = AnalyzePacket(buf, size);
		return analysis.isValid ? analysis.headerSize : 0;
	}

	bool PacketBuilder::IsValidPacket(BYTE* buf, uint32 size)
	{
		PacketAnalysis analysis = AnalyzePacket(buf, size);
		return analysis.isValid;
	}



	Sptr<SendBuffer> PacketBuilder::CreatePacketInternal(uint8 type, uint8 id, uint8 flags, const void* payload, uint32 payloadSize, uint16 seq, uint8 fragIndex, uint8 fragTotal)
	{
		bool isReliable = HasFlag(flags, PacketFlags::RELIABLE);
		bool isFragmented = HasFlag(flags, PacketFlags::FRAGMENTED);

		uint32 headerSize;

		if (isFragmented)
			headerSize = PacketHeader::GetFullSize();
		else if (isReliable)
			headerSize = PacketHeader::GetHalfSize();
		else
			headerSize = PacketHeader::GetBaseSize();

		const uint32 totalSize = headerSize + payloadSize;
		const uint32 allocSize = isFragmented ? totalSize : totalSize + sizeof(AckHeader);

		Sptr<SendBuffer> buf = SendBufferManager::Instance().Open(allocSize);
		BufferWriter bw(buf->Buffer(), buf->AllocSize());

		PacketHeader header(type, id, totalSize, flags, seq, fragIndex, fragTotal);

		bw.WriteBytes(&header, headerSize);

		if (payload && payloadSize != 0)
			bw.WriteBytes(payload, payloadSize);

		buf->Close(bw.WriteSize());
		return buf;
	}
}
