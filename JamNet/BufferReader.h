#pragma once

namespace jam::net
{
	class BufferReader
	{
	public:
		BufferReader() = default;
		BufferReader(BYTE* buffer, uint32 size, uint32 pos = 0);
		~BufferReader() = default;

		BYTE*			Buffer() { return m_buffer; }
		uint32			Size() { return m_size; }
		uint32			ReadSize() { return m_pos; }
		uint32			FreeSize() { return m_size - m_pos; }


		template<typename T>
		bool			Peek(T* dest) { return Peek(dest, sizeof(T)); }
		bool			Peek(void* dest, uint32 len);

		template<typename T>
		bool			Read(T* dest) { return Read(dest, sizeof(T)); }
		bool			Read(void* dest, uint32 len);

		template<typename T>
		BufferReader&	operator>>(OUT T& dest);


	private:
		BYTE*			m_buffer = nullptr;
		uint32			m_size = 0;
		uint32			m_pos = 0;

	};

	template<typename T>
	inline BufferReader& BufferReader::operator>>(OUT T& dest)
	{
		dest = *reinterpret_cast<T*>(&m_buffer[m_pos]);
		m_pos += sizeof(T);
		return *this;
	}

}
