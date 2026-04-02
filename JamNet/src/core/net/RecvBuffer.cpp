#include "pch.h"
#include "jamnet/core/net/RecvBuffer.h"

namespace jam::net
{
	/*----------------
		RecvBuffer
	-----------------*/

	RecvBuffer::RecvBuffer(int32 bufferSize, int32 count)
		: m_bufferSize(bufferSize)
	{
		m_capacity = bufferSize * count;
		m_buffer.resize(m_capacity);
	}


	void RecvBuffer::Init(int32 bufferSize, int32 count)
	{
		m_bufferSize = bufferSize;
		m_capacity   = bufferSize * std::max(1, count);
		m_buffer.clear();
		m_buffer.resize(m_capacity);
		m_readPos   = 0;
		m_writePos  = 0;
	}

	void RecvBuffer::Clean()
	{
		int32 dataSize = DataSize();

		if (dataSize == 0)
		{
			m_readPos = m_writePos = 0;
		}
		else
		{
			if (FreeSize() > m_bufferSize)
			{
				::memcpy(m_buffer.data(), &m_buffer[m_readPos], dataSize);
				m_readPos  = 0;
				m_writePos = dataSize;
			}
		}
	}

	bool RecvBuffer::OnRead(int32 numOfBytes)
	{
		if (numOfBytes > DataSize())
			return false;

		m_readPos += numOfBytes;
		return true;
	}

	bool RecvBuffer::OnWrite(int32 numOfBytes)
	{
		if (numOfBytes > FreeSize())
			return false;

		m_writePos += numOfBytes;
		return true;
	}

}
