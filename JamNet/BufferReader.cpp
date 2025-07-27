#include "pch.h"
#include "BufferReader.h"

namespace jam::net
{

	//BufferReader::BufferReader(BYTE* buffer, uint32 size, uint32 pos)
	//	: m_buffer(buffer), m_size(size), m_pos(pos)
	//{
	//}


	//bool BufferReader::Peek(void* dest, uint32 len)
	//{
	//	if (FreeSize() < len)
	//		return false;

	//	::memcpy(dest, &m_buffer[m_pos], len);
	//	return true;
	//}

	//bool BufferReader::Read(void* dest, uint32 len)
	//{
	//	if (Peek(dest, len) == false)
	//		return false;

	//	m_pos += len;
	//	return true;
	//}

}
