#include "pch.h"
#include "PacketBuilder.h"

#include "FragmentHandler.h"

namespace jam::net
{
	void PacketBuilder::BeginWrite(uint32 allocSize)
	{
		m_sendBuffer = SendBufferManager::Instance().Open(allocSize);
		m_bufferWriter = std::make_unique<BufferWriter>(m_sendBuffer->Buffer(), m_sendBuffer->AllocSize());	// todo : change to object pool
	}

	void PacketBuilder::EndWrite()
	{
		if (m_bufferWriter)
		{
			uint32 writeSize = m_bufferWriter->WriteSize();
			m_sendBuffer->Close(writeSize);
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
