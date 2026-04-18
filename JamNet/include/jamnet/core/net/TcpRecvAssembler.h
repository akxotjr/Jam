#pragma once
#include "jamnet/core/net/Buffer.h"


namespace jam::net
{

	class TcpRecvAssembler
	{
	public:
		enum class eAssembleResult : uint8
		{
			Ok,
			NeedMoreData,
			ProtocolError,
		};

		static constexpr uint32 kDefaultCapacity = 16 * 1024;
		static constexpr uint32 kCompactThreshold = 4 * 1024;
		static constexpr uint32 kHardMaxCapacity = BufferBlock::k_blockSize;

	public:
		explicit TcpRecvAssembler(BufferPool& pool) : m_pool(pool) {}

	public:
		bool				Init(uint32 initialCapacity = kDefaultCapacity);
		void				Reset();
		bool				IsInitialized() const { return m_storage.IsValid(); }
		uint32				BufferedBytes() const { return m_writeOffset - m_readOffset; }

		// scratch recv slot에서 받은 bytes를 누적 버퍼로 복사
		bool				Append(const BYTE* src, uint32 bytes);

		eAssembleResult		TryExtractPacket(Packet& out);

	private:
		void				ResetWindow();
		void				SyncSliceWindow();
		bool				EnsureWritable(uint32 bytes);
		void				Compact();
		void				CompactIfNeeded();
		bool				Grow(uint32 additionalBytes);

	private:
		BufferPool&			m_pool;
		Packet				m_storage     = {};
		uint32				m_readOffset  = 0;
		uint32				m_writeOffset = 0;
	};
}
