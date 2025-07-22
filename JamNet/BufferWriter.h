#pragma once

namespace jam::net
{
	class BufferWriter
	{
	public:
		BufferWriter() = default;
		BufferWriter(BYTE* buffer, uint32 size, uint32 pos = 0);
		~BufferWriter() = default;

		BYTE*			Buffer() { return m_buffer; }
		uint32			Size() { return m_size; }
		uint32			WriteSize() { return m_pos; }
		uint32			FreeSize() { return m_size - m_pos; }


		template<typename T>
		bool			Write(T* src) { return Write(src, sizeof(T)); }
		bool			Write(void* src, uint32 len);

		template<typename T>
		T*				Reserve();


		template<typename T>
		BufferWriter&	operator<<(T&& src);


	private:
		BYTE*			m_buffer = nullptr;
		uint32			m_size = 0;
		uint32			m_pos = 0;
	};

	template<typename T>
	T* BufferWriter::Reserve()
	{
		if (FreeSize() < sizeof(T))
			return nullptr;

		T* ret = reinterpret_cast<T*>(&m_buffer[m_pos]);
		m_pos += sizeof(T);

		return ret;
	}

	template<typename T>
	BufferWriter& BufferWriter::operator<<(T&& src)
	{
		using DataType = std::remove_reference_t<T>;
		*reinterpret_cast<DataType*>(&m_buffer[m_pos]) = std::forward<DataType>(src);
		m_pos += sizeof(T);
		return *this;
	}
}
