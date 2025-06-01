#pragma once


namespace jam::net
{
	/*----------------
		SendBuffer
	-----------------*/

	class SendBufferChunk;

	class SendBuffer : public enable_shared_from_this<SendBuffer>
	{
	public:
		SendBuffer(Sptr<SendBufferChunk> owner, BYTE* buffer, int32 allocSize);
		~SendBuffer() = default;

		BYTE*										Buffer() { return m_buffer; }
		uint32										AllocSize() { return m_allocSize; }
		uint32										WriteSize() { return m_writeSize; }
		void										Close(uint32 writeSize);

	private:
		BYTE*										m_buffer;
		uint32										m_allocSize = 0;
		uint32										m_writeSize = 0;
		Sptr<SendBufferChunk>						m_owner;
	};

	/*---------------------
		SendBufferChunk
	----------------------*/

	class SendBufferChunk : public enable_shared_from_this<SendBufferChunk>
	{
		enum
		{
			SEND_BUFFER_CHUNK_SIZE = 6000
		};

	public:
		SendBufferChunk();
		~SendBufferChunk();

		void										Reset();
		Sptr<SendBuffer>							Open(uint32 allocSize);
		void										Close(uint32 writeSize);

		bool										IsOpen() { return m_open; }
		BYTE*										Buffer() { return &m_buffer[m_usedSize]; }
		uint32										FreeSize() { return static_cast<uint32>(m_buffer.size()) - m_usedSize; }

	private:
		xarray<BYTE, SEND_BUFFER_CHUNK_SIZE>		m_buffer = {};
		bool										m_open = false;
		uint32										m_usedSize = 0;
	};



	/*---------------------
	   SendBufferManager
	----------------------*/

	class SendBufferManager
	{
		DECLARE_SINGLETON(SendBufferManager)

	public:
		Sptr<SendBuffer>							Open(uint32 size);

	private:
		Sptr<SendBufferChunk>						Pop();
		void										Push(Sptr<SendBufferChunk> buffer);
		static void									PushGlobal(SendBufferChunk* buffer);

	private:
		USE_LOCK
		xvector<Sptr<SendBufferChunk>>				m_sendBufferChunks;
	};


	extern thread_local Sptr<SendBufferChunk> tl_SendBufferChunk;
}

