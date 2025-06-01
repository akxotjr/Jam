#pragma once

namespace jam::net
{
	/*----------------
	RecvBuffer
-----------------*/


	class RecvBuffer
	{
		enum { BUFFER_COUNT = 10 };

	public:
		RecvBuffer(int32 bufferSize);
		~RecvBuffer() = default;

		void			Clean();
		bool			OnRead(int32 numOfBytes);
		bool			OnWrite(int32 numOfBytes);

		BYTE*			ReadPos() { return &m_buffer[m_readPos]; }
		BYTE*			WritePos() { return &m_buffer[m_writePos]; }
		int32			DataSize() { return m_writePos - m_readPos; }
		int32			FreeSize() { return m_capacity - m_writePos; }

	private:
		int32			m_capacity = 0;
		int32			m_bufferSize = 0;
		int32			m_readPos = 0;
		int32			m_writePos = 0;
		xvector<BYTE>	m_buffer;
	};
}

