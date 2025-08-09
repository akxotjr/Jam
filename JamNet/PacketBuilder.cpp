#include "pch.h"
#include "PacketBuilder.h"

#include "FragmentHandler.h"

namespace jam::net
{
	void PacketBuilder::BeginWrite(ePacketMode mode, uint32 allocSize)
	{
		m_mode = mode;

		switch (mode)
		{
		case ePacketMode::SMALL_PKT:
			m_sendBuffer = SendBufferManager::Instance().Open(allocSize);
			break;
		case ePacketMode::GAME_PKT:
			m_sendBuffer = SendBufferManager::Instance().Open(MTU);
			break;
		case ePacketMode::LARGE_PKT:
			// todo : fragment
			break;
		}

		m_bufferWriter = std::make_unique<BufferWriter>(m_sendBuffer->Buffer(), m_sendBuffer->AllocSize());	// todo : change to object pool
	}

	void PacketBuilder::EndWrite()
	{
		if (m_bufferWriter)
		{
			m_sendBuffer->Close(m_bufferWriter->WriteSize());
			m_bufferWriter.reset();
		}
	}

	void PacketBuilder::BeginRead(BYTE* buffer, uint32 size)
	{
		m_bufferReader = std::make_unique<BufferReader>(buffer, size);
	}

	void PacketBuilder::EndRead()
	{
		m_bufferReader.reset();
	}
}
