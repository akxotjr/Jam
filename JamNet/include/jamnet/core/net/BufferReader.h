#pragma once

namespace jam::net
{
	class BufferReader
	{
	public:
		BufferReader() = default;
		BufferReader(BYTE* buffer, uint32 size, uint32 pos = 0)
			: m_buffer(buffer), m_size(size), m_pos(pos) {}

		BYTE*	Buffer()   const { return m_buffer; }
		uint32	Size()	   const { return m_size; }
		uint32	ReadSize() const { return m_pos; }
		uint32	FreeSize() const { return m_size - m_pos; }

		template<typename T>
		bool Peek(T& dest)
		{
			static_assert(std::is_trivially_copyable_v<T>, "Only trivially copyable types allowed");

			if (FreeSize() < sizeof(T))
				return false;

			std::memcpy(&dest, &m_buffer[m_pos], sizeof(T));
			return true;
		}

		template<typename T>
		bool Read(T& dest)
		{
			if (!Peek(dest))
				return false;

			m_pos += sizeof(T);
			return true;
		}

		bool PeekBytes(void* dest, uint32 len)
		{
			if (FreeSize() < len)
				return false;

			std::memcpy(dest, &m_buffer[m_pos], len);
			return true;
		}

		bool ReadBytes(void* dest, uint32 len)
		{
			if (!PeekBytes(dest, len))
				return false;

			m_pos += len;
			return true;
		}

		template<typename T>
		BufferReader& operator>>(T& dest)
		{
			bool success = Read(dest);
			assert(success && "BufferReader out of bounds");
			return *this;
		}

	private:
		BYTE*	m_buffer = nullptr;
		uint32	m_size   = 0;
		uint32	m_pos	 = 0;
	};

}

