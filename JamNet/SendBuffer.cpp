#include "pch.h"
#include "SendBuffer.h"

namespace jam::net
{

	thread_local Sptr<SendBufferChunk> tl_SendBufferChunk;

	/*----------------
		SendBuffer
	-----------------*/

	SendBuffer::SendBuffer(Sptr<SendBufferChunk> owner, BYTE* buffer, int32 allocSize)
		: m_owner(owner), m_buffer(buffer), m_allocSize(allocSize)
	{

	}

	void SendBuffer::Close(uint32 writeSize)
	{
		ASSERT_CRASH(m_allocSize >= writeSize);
		m_writeSize = writeSize;

		m_owner->Close(writeSize);
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

	Sptr<SendBuffer> SendBufferChunk::Open(uint32 allocSize)
	{
		ASSERT_CRASH(allocSize <= SEND_BUFFER_CHUNK_SIZE);
		ASSERT_CRASH(m_open == false);

		if (allocSize > FreeSize())
			return nullptr;

		m_open = true;

		return utils::memory::ObjectPool<SendBuffer>::MakeShared(shared_from_this(), Buffer(), allocSize);
	}

	void SendBufferChunk::Close(uint32 writeSize)
	{
		ASSERT_CRASH(m_open == true);

		m_open = false;
		m_usedSize += writeSize;
	}




	/*---------------------
	   SendBufferManager
	----------------------*/

	Sptr<SendBuffer> SendBufferManager::Open(uint32 size)
	{
		if (tl_SendBufferChunk == nullptr)
		{
			tl_SendBufferChunk = Pop();
			tl_SendBufferChunk->Reset();
		}

		ASSERT_CRASH(tl_SendBufferChunk->IsOpen() == false);

		if (tl_SendBufferChunk->FreeSize() < size)
		{
			tl_SendBufferChunk = Pop();
			tl_SendBufferChunk->Reset();
		}

		return tl_SendBufferChunk->Open(size);
	}

	Sptr<SendBufferChunk> SendBufferManager::Pop()
	{
		{
			WRITE_LOCK
			if (m_sendBufferChunks.empty() == false)
			{
				Sptr<SendBufferChunk> sendBufferChunk = m_sendBufferChunks.back();
				m_sendBufferChunks.pop_back();
				return sendBufferChunk;
			}
		}
		return Sptr<SendBufferChunk>(utils::memory::xnew<SendBufferChunk>(), PushGlobal);
	}

	void SendBufferManager::Push(Sptr<SendBufferChunk> buffer)
	{
		WRITE_LOCK
		m_sendBufferChunks.push_back(buffer);
	}

	void SendBufferManager::PushGlobal(SendBufferChunk* buffer)
	{
		SendBufferManager::Instance()->Push(Sptr<SendBufferChunk>(buffer, PushGlobal));
	}

}
