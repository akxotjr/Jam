#pragma once

namespace jam::net
{
	class BufferWriter
	{
	public:
		BufferWriter() = default;
		BufferWriter(BYTE* buffer, uint32 size, uint32 pos = 0)
			: m_buffer(buffer), m_size(size), m_pos(pos) {}

		BYTE*	Buffer()	const { return m_buffer; }
		uint32	Size()		const { return m_size; }
		uint32	WriteSize() const { return m_pos; }
		uint32	FreeSize()	const { return m_size - m_pos; }


		template<typename T>
		bool Write(const T& value)
		{
			static_assert(std::is_trivially_copyable_v<T>, "Only trivially copyable types allowed");

			if (FreeSize() < sizeof(T))
				return false;

			std::memcpy(&m_buffer[m_pos], &value, sizeof(T));
			m_pos += sizeof(T);
			return true;
		}

		bool WriteBytes(const void* data, uint32 len)
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
		uint32		m_size   = 0;
		uint32		m_pos    = 0;
	};
}
