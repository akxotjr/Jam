#pragma once

namespace jam::net
{
	class BufferReader
	{
	public:
		BufferReader() = default;
		BufferReader(BYTE* buffer, uint32 size, uint32 pos = 0)
			: m_buffer(buffer), m_size(size), m_pos(pos) {}

		BYTE* Buffer() const { return m_buffer; }
		uint32	Size() const { return m_size; }
		uint32	ReadSize() const { return m_pos; }
		uint32	FreeSize() const { return m_size - m_pos; }

		template<typename T>
		bool Peek(T& dest)
		{
			static_assert(std::is_trivially_copyable_v<T>, "Only trivially copyable types allowed");

			if (FreeSize() < sizeof(T))
				return false;

			std::memcpy(&dest, &m_buffer[m_pos], sizeof(T));
			dest = FromBigEndian(dest);
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
		uint32	m_size = 0;
		uint32	m_pos = 0;
	};

	// --- Endian 변환 함수 (Big Endian 기준) ---
	inline uint16 FromBigEndian(uint16 v)
	{
		return (v >> 8) | (v << 8);
	}

	inline uint32 FromBigEndian(uint32 v)
	{
		return ((v & 0x000000FF) << 24) |
			((v & 0x0000FF00) << 8) |
			((v & 0x00FF0000) >> 8) |
			((v & 0xFF000000) >> 24);
	}

	inline uint64 FromBigEndian(uint64 v)
	{
		return ((v & 0x00000000000000FFULL) << 56) |
			((v & 0x000000000000FF00ULL) << 40) |
			((v & 0x0000000000FF0000ULL) << 24) |
			((v & 0x00000000FF000000ULL) << 8) |
			((v & 0x000000FF00000000ULL) >> 8) |
			((v & 0x0000FF0000000000ULL) >> 24) |
			((v & 0x00FF000000000000ULL) >> 40) |
			((v & 0xFF00000000000000ULL) >> 56);
	}

	template<typename T>
	inline T FromBigEndian(const T& v)
	{
		// 기본형 외에는 그대로 반환
		return v;
	}
}
	//class BufferReader
	//{
	//public:
	//	BufferReader() = default;
	//	BufferReader(BYTE* buffer, uint32 size, uint32 pos = 0);
	//	~BufferReader() = default;

	//	BYTE*			Buffer() { return m_buffer; }
	//	uint32			Size() { return m_size; }
	//	uint32			ReadSize() { return m_pos; }
	//	uint32			FreeSize() { return m_size - m_pos; }


	//	template<typename T>
	//	bool			Peek(T* dest) { return Peek(dest, sizeof(T)); }
	//	bool			Peek(void* dest, uint32 len);

	//	template<typename T>
	//	bool			Read(T* dest) { return Read(dest, sizeof(T)); }
	//	bool			Read(void* dest, uint32 len);

	//	template<typename T>
	//	BufferReader&	operator>>(OUT T& dest);


	//private:
	//	BYTE*			m_buffer = nullptr;
	//	uint32			m_size = 0;
	//	uint32			m_pos = 0;

	//};

	//template<typename T>
	//inline BufferReader& BufferReader::operator>>(OUT T& dest)
	//{
	//	dest = *reinterpret_cast<T*>(&m_buffer[m_pos]);
	//	m_pos += sizeof(T);
	//	return *this;
	//}

}
