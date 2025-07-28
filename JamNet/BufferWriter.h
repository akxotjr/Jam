#pragma once

namespace jam::net
{
	class BufferWriter
	{
	public:
		BufferWriter() = default;
		BufferWriter(BYTE* buffer, uint32 size, uint32 pos = 0)
			: m_buffer(buffer), m_size(size), m_pos(pos) {}

		BYTE*	Buffer() const { return m_buffer; }
		uint32	Size() const { return m_size; }
		uint32	WriteSize() const { return m_pos; }
		uint32	FreeSize() const { return m_size - m_pos; }


		template<typename T>
		bool Write(const T& value)
		{
			static_assert(std::is_trivially_copyable_v<T>, "Only trivially copyable types allowed");

			if (FreeSize() < sizeof(T))
				return false;

			T temp = ToBigEndian(value); // endian 변환
			std::memcpy(&m_buffer[m_pos], &temp, sizeof(T));
			m_pos += sizeof(T);
			return true;
		}

		bool WriteBytes(const void* data, uint32_t len)
		{
			if (FreeSize() < len)
				return false;

			std::memcpy(&m_buffer[m_pos], data, len);
			m_pos += len;
			return true;
		}

		template<typename T>
		T* Reserve()
		{
			if (FreeSize() < sizeof(T))
				return nullptr;

			T* ret = reinterpret_cast<T*>(&m_buffer[m_pos]);
			m_pos += sizeof(T);
			return ret;
		}

		template<typename T>
		BufferWriter& operator<<(const T& value)
		{
			Write(value);
			return *this;
		}

	private:
		BYTE*		m_buffer = nullptr;
		uint32		m_size = 0;
		uint32		m_pos = 0;
	};

	// 기본 Endian 변환 함수 (big endian 사용)
	inline uint16_t ToBigEndian(uint16_t v)
	{
		return (v >> 8) | (v << 8);
	}

	inline uint32_t ToBigEndian(uint32_t v)
	{
		return ((v & 0x000000FF) << 24) |
			((v & 0x0000FF00) << 8) |
			((v & 0x00FF0000) >> 8) |
			((v & 0xFF000000) >> 24);
	}

	inline uint64_t ToBigEndian(uint64_t v)
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

	// 기본형 외에는 그대로 반환
	template<typename T>
	inline T ToBigEndian(const T& v)
	{
		return v;
	}
}

	//class BufferWriter
	//{
	//public:
	//	BufferWriter() = default;
	//	BufferWriter(BYTE* buffer, uint32 size, uint32 pos = 0);
	//	~BufferWriter() = default;

	//	BYTE*			Buffer() { return m_buffer; }
	//	uint32			Size() { return m_size; }
	//	uint32			WriteSize() { return m_pos; }
	//	uint32			FreeSize() { return m_size - m_pos; }


	//	template<typename T>
	//	bool			Write(T* src) { return Write(src, sizeof(T)); }
	//	bool			Write(void* src, uint32 len);

	//	template<typename T>
	//	T*				Reserve();


	//	template<typename T>
	//	BufferWriter&	operator<<(T&& src);


	//private:
	//	BYTE*			m_buffer = nullptr;
	//	uint32			m_size = 0;
	//	uint32			m_pos = 0;
	//};

	//template<typename T>
	//T* BufferWriter::Reserve()
	//{
	//	if (FreeSize() < sizeof(T))
	//		return nullptr;

	//	T* ret = reinterpret_cast<T*>(&m_buffer[m_pos]);
	//	m_pos += sizeof(T);

	//	return ret;
	//}

	//template<typename T>
	//BufferWriter& BufferWriter::operator<<(T&& src)
	//{
	//	using DataType = std::remove_reference_t<T>;
	//	*reinterpret_cast<DataType*>(&m_buffer[m_pos]) = std::forward<DataType>(src);
	//	m_pos += sizeof(T);
	//	return *this;
	//}

