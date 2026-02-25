#pragma once

namespace jam::net
{
	/*----------------
		RecvBuffer
	-----------------*/


	class RecvBuffer
	{
	public:
		RecvBuffer() = default;
		RecvBuffer(int32 bufferSize, int32 count);
		~RecvBuffer() = default;

		void			Init(int32 bufferSize, int32 count);
		void			Clean();
		bool			OnRead(int32 numOfBytes);
		bool			OnWrite(int32 numOfBytes);

		BYTE*			ReadPos() { return &m_buffer[m_readPos]; }
		BYTE*			WritePos() { return &m_buffer[m_writePos]; }
		int32			DataSize() const { return m_writePos - m_readPos; }
		int32			FreeSize() const { return m_capacity - m_writePos; }

		static shared_ptr<RecvBuffer> FromSpan(const BYTE* data, uint32 size)
		{
			auto buf = MakeShared<RecvBuffer>();
			buf->Init(size, 1);
			if (size) ::memcpy(buf->WritePos(), data, size);
			buf->OnWrite(size);
			return buf;
		}

	private:
		int32			m_capacity = 0;
		int32			m_bufferSize = 0;
		int32			m_readPos = 0;
		int32			m_writePos = 0;
		vector<BYTE>	m_buffer;
	};
}

