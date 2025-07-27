#include "pch.h"
#include "PacketBuilder.h"

namespace jam::net
{
	void PacketBuilder::BeginWrite(uint32 allocSize)
	{
		m_sendBuffer = SendBufferManager::Instance().Open(allocSize);
		m_bufferWriter = std::make_unique<BufferWriter>(m_sendBuffer->Buffer(), m_sendBuffer->AllocSize());
	}
	void PacketBuilder::EndWrite()
	{
		
	}
}
