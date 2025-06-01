#include "pch.h"
#include "RecvBuffer.h"

namespace jam::net
{
	/*----------------
	RecvBuffer
-----------------*/

	RecvBuffer::RecvBuffer(int32 bufferSize) : m_bufferSize(bufferSize)
	{
		m_capacity = bufferSize * BUFFER_COUNT;
		m_buffer.resize(m_capacity);
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
				::memcpy(&m_buffer[0], &m_buffer[m_readPos], dataSize);
				m_readPos = 0;
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
