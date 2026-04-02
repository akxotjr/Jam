#pragma once


namespace jam::net
{
	/*----------------
		SendBuffer
	-----------------*/

	class SendBufferChunk;

	class SendBuffer : public std::enable_shared_from_this<SendBuffer>
	{
	public:
		SendBuffer(std::shared_ptr<SendBufferChunk> owner, BYTE* buffer, int32 allocSize);
		~SendBuffer() = default;

		BYTE*										Buffer() const { return m_buffer; }
		uint32										AllocSize() const { return m_allocSize; }
		uint32										WriteSize() const { return m_writeSize; }
		void										SetWriteSize(uint32 writeSize);
		void										Close(uint32 writeSize);
		void										CloseWithReserve(uint32 writeSize, uint32 reserveSize);

	private:
		BYTE*										m_buffer;
		uint32										m_allocSize = 0;
		uint32										m_writeSize = 0;
		std::shared_ptr<SendBufferChunk>			m_owner;
	};

	/*---------------------
		SendBufferChunk
	----------------------*/

	class SendBufferChunk : public std::enable_shared_from_this<SendBufferChunk>
	{
		enum
		{
			SEND_BUFFER_CHUNK_SIZE = 6000
		};

	public:
		SendBufferChunk();
		~SendBufferChunk();

		void										Reset();
		std::shared_ptr<SendBuffer>					Open(uint32 allocSize);
		void										Close(uint32 commitedSize);

		bool										IsOpen() { return m_open; }
		BYTE*										Buffer() { return &m_buffer[m_usedSize]; }
		uint32										FreeSize() { return static_cast<uint32>(m_buffer.size()) - m_usedSize; }

	private:
		std::array<BYTE, SEND_BUFFER_CHUNK_SIZE>	m_buffer = {};
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
		std::shared_ptr<SendBuffer>					Open(uint32 size);

	private:
		std::shared_ptr<SendBufferChunk>			Pop();
		void										Push(std::shared_ptr<SendBufferChunk> buffer);
		static void									PushGlobal(SendBufferChunk* buffer);

	private:
		USE_LOCK
		std::vector<std::shared_ptr<SendBufferChunk>>	m_sendBufferChunks;
	};


	extern thread_local std::shared_ptr<SendBufferChunk> tl_SendBufferChunk;
}

