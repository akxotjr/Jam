#include "pch.h"
#include "PacketBuilder.h"
#include "BufferReader.h"


namespace jam::net
{
	Sptr<SendBuffer> PacketBuilder::CreatePacket(ePacketType type, uint8 id, uint8 flags, eChannelType channel, const void* payload, uint32 payloadSize, uint16 seq, uint8 fragIndex, uint8 fragTotal)
	{
		return CreatePacketInternal(E2U(type), id, flags, E2U(channel), payload, payloadSize, seq, fragIndex, fragTotal);
	}

	Sptr<SendBuffer> PacketBuilder::CreateSystemPacket(eSystemPacketId id, uint8 flags, eChannelType channel, const void* payload, uint32 payloadSize, uint16 seq)
	{
		return CreatePacketInternal(E2U(ePacketType::SYSTEM), E2U(id), flags, E2U(channel), payload, payloadSize, seq);
	}

	Sptr<SendBuffer> PacketBuilder::CreateHandshakePacket(eSystemPacketId id)
	{
		return CreateSystemPacket(id, PacketFlags::NONE, eChannelType::RELIABLE_ORDERED, nullptr, 0);
	}

	Sptr<SendBuffer> PacketBuilder::CreatePingPacket(const PING& ping, uint16 seq)
	{
		return CreateSystemPacket(eSystemPacketId::PING, PacketFlags::NONE, eChannelType::UNRELIABLE_SEQUENCED, &ping, sizeof(ping), seq);
	}

	Sptr<SendBuffer> PacketBuilder::CreatePongPacket(const PONG& pong, uint16 seq)
	{
		return CreateSystemPacket(eSystemPacketId::PING, PacketFlags::NONE, eChannelType::UNRELIABLE_SEQUENCED, &pong, sizeof(pong), seq);
	}

	Sptr<SendBuffer> PacketBuilder::CreateReliabilityAckPacket(uint16 seq, uint32 bitfield)
	{
		constexpr uint32 size = sizeof(PacketHeader) + sizeof(AckHeader);
		Sptr<SendBuffer> buf = SendBufferManager::Instance().Open(size);
		BufferWriter bw(buf->Buffer(), buf->AllocSize());

		// todo

		buf->Close(bw.WriteSize());

		return buf;
	}

	Sptr<SendBuffer> PacketBuilder::CreateNackPacket(uint16 seq, uint32 bitfield)
	{
		return Sptr<SendBuffer>();
	}

	Sptr<SendBuffer> PacketBuilder::CreateRpcPacket(eRpcPacketId id, uint8 flags, uint8 channel, const void* payload, uint32 payloadSize, uint16 seq)
	{
		return CreatePacketInternal(E2U(ePacketType::RPC), E2U(id), flags, channel, payload, payloadSize, seq);
	}


	PacketAnalysis PacketBuilder::AnalyzePacket(BYTE* buf, uint32 size)
	{
		PacketAnalysis result = {};

		if (size < PacketHeader::GetBaseSize())
			return result;

		BufferReader br(buf, size);

		if (!br.PeekBytes(&result.header, PacketHeader::GetBaseSize()))
			return {};

		if (!result.header.IsValid())
			return {};

		result.headerSize = result.header.GetActualSize();

		if (size < result.headerSize)
			return {};

		if (!br.ReadBytes(&result.header, result.headerSize))
			return {};

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



	Sptr<SendBuffer> PacketBuilder::CreatePacketInternal(uint8 type, uint8 id, uint8 flags, uint8 channel, const void* payload, uint32 payloadSize, uint16 seq, uint8 fragIndex, uint8 fragTotal)
	{
		bool isReliable = HasReliable(U2E(eChannelType, channel));
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

		// todo : size를 직접 계산해서 넣는게 맘에 안듦
		PacketHeader header(type, id, totalSize, flags, channel, seq, fragIndex, fragTotal);

		bw.WriteBytes(&header, headerSize);

		if (payload && payloadSize != 0)
			bw.WriteBytes(payload, payloadSize);

		buf->Close(bw.WriteSize());
		return buf;
	}
}
