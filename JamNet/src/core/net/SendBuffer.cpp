#include "pch.h"
#include "jamnet/core/net/SendBuffer.h"

namespace jam::net
{

	thread_local std::shared_ptr<SendBufferChunk> tl_SendBufferChunk;

	/*----------------
		SendBuffer
	-----------------*/

	SendBuffer::SendBuffer(std::shared_ptr<SendBufferChunk> owner, BYTE* buffer, int32 allocSize)
		: m_owner(owner), m_buffer(buffer), m_allocSize(allocSize)
	{

	}

	void SendBuffer::SetWriteSize(uint32 writeSize)
	{
		JAM_ASSERT(m_allocSize >= writeSize);
		m_writeSize = writeSize;
	}

	void SendBuffer::Close(uint32 writeSize)
	{
		JAM_ASSERT(m_allocSize >= writeSize);
		m_writeSize = writeSize;

		m_owner->Close(writeSize);
	}

	void SendBuffer::CloseWithReserve(uint32 writeSize, uint32 reserveSize)
	{
		JAM_ASSERT(m_allocSize >= writeSize);
		JAM_ASSERT(m_allocSize >= reserveSize);
		m_writeSize = writeSize;
		m_owner->Close(reserveSize);
	}


	/*---------------------
		SendBufferChunk
	----------------------*/

	SendBufferChunk::SendBufferChunk()
	{
	}

	SendBufferChunk::~SendBufferChunk()
	{
	}

	void SendBufferChunk::Reset()
	{
		m_open = false;
		m_usedSize = 0;
	}

	std::shared_ptr<SendBuffer> SendBufferChunk::Open(uint32 allocSize)
	{
		JAM_ASSERT(allocSize <= SEND_BUFFER_CHUNK_SIZE);
		JAM_ASSERT(m_open == false);

		if (allocSize > FreeSize())
			return nullptr;

		m_open = true;

		return ObjectPool<SendBuffer>::MakeShared(shared_from_this(), Buffer(), allocSize);
	}

	void SendBufferChunk::Close(uint32 commitedSize)
	{
		JAM_ASSERT(m_open == true);

		m_open = false;
		m_usedSize += commitedSize;
	}




	/*---------------------
	   SendBufferManager
	----------------------*/

	std::shared_ptr<SendBuffer> SendBufferManager::Open(uint32 size)
	{
		if (tl_SendBufferChunk == nullptr)
		{
			tl_SendBufferChunk = Pop();
			tl_SendBufferChunk->Reset();
		}

		JAM_ASSERT(tl_SendBufferChunk->IsOpen() == false);

		if (tl_SendBufferChunk->FreeSize() < size)
		{
			tl_SendBufferChunk = Pop();
			tl_SendBufferChunk->Reset();
		}

		return tl_SendBufferChunk->Open(size);
	}

	std::shared_ptr<SendBufferChunk> SendBufferManager::Pop()
	{
		{
			WRITE_LOCK
			if (m_sendBufferChunks.empty() == false)
			{
				std::shared_ptr<SendBufferChunk> sendBufferChunk = m_sendBufferChunks.back();
				m_sendBufferChunks.pop_back();
				return sendBufferChunk;
			}
		}
		return std::shared_ptr<SendBufferChunk>(xnew<SendBufferChunk>(), PushGlobal);
	}

	void SendBufferManager::Push(std::shared_ptr<SendBufferChunk> buffer)
	{
		WRITE_LOCK
		m_sendBufferChunks.push_back(buffer);
	}

	void SendBufferManager::PushGlobal(SendBufferChunk* buffer)
	{
		SendBufferManager::Instance().Push(std::shared_ptr<SendBufferChunk>(buffer, PushGlobal));
	}

}
