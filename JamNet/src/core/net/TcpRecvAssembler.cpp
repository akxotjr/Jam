#include "pch.h"
#include "jamnet/core/net/TcpRecvAssembler.h"
#include "jamnet/core/net/PacketBuilder.h"


namespace jam::net
{
	bool TcpRecvAssembler::Init(uint32 initialCapacity)
	{
		if (m_storage.IsValid())
			return true;

		BufWriter writer(m_pool);
		BufferSlice slice = writer.OpenForPayload(initialCapacity);
		slice.CommitReserved(initialCapacity);
		m_storage = MakeOwned(slice);

		ResetWindow();
		return true;
	}

	void TcpRecvAssembler::Reset()
	{
		m_storage.Reset();
		m_readOffset  = 0;
		m_writeOffset = 0;
	}

	bool TcpRecvAssembler::Append(const BYTE* src, uint32 bytes)
	{
		JAM_ASSERT(src != nullptr || bytes == 0);

		if (bytes == 0)
			return true;

		if (!IsInitialized())
		{
			if (!Init())
				return false;
		}

		if (!EnsureWritable(bytes))
			return false;

		BYTE* dst = m_storage->Begin() + m_writeOffset;
		std::memcpy(dst, src, bytes);
		m_writeOffset += bytes;

		SyncSliceWindow();
		return true;
	}

	TcpRecvAssembler::eAssembleResult TcpRecvAssembler::TryExtractPacket(Packet& out)
	{
		out.Reset();

		const uint32 available = BufferedBytes();
		if (available < PacketHeader::BASE_SIZE)
			return eAssembleResult::NeedMoreData;

		BYTE* base = m_storage->Begin() + m_readOffset;

		// 1) 최소 헤더 3바이트는 있으니, 고정 비트만 먼저 해석 가능
		PacketHeader* header = reinterpret_cast<PacketHeader*>(base);

		if (!header->IsValid())
			return eAssembleResult::ProtocolError;

		const uint32 headerSize = header->GetActualSize();

		// 2) 가변 헤더 전체가 아직 안 왔음
		if (available < headerSize)
			return eAssembleResult::NeedMoreData;

		const uint32 totalSize = header->GetSize();

		// 3) 말이 안 되는 size
		if (totalSize < headerSize)
			return eAssembleResult::ProtocolError;

		// 4) 아직 payload/body가 다 안 왔음
		if (available < totalSize)
			return eAssembleResult::NeedMoreData;

		// 5) 여기까지 왔으면 하나의 완성 packet 존재
		BufferSlice pktSlice = m_storage->SliceVisible(m_readOffset, totalSize);
		out = MakeOwned(pktSlice);

		m_readOffset += totalSize;
		SyncSliceWindow();
		CompactIfNeeded();

		return eAssembleResult::Ok;
	}

	void TcpRecvAssembler::ResetWindow()
	{
		m_readOffset = 0;
		m_writeOffset = 0;
		SyncSliceWindow();
	}

	void TcpRecvAssembler::SyncSliceWindow()
	{
		if (!m_storage.IsValid())
			return;

		auto& s = m_storage.Get();
		s.head   = s.begin;
		s.data   = s.begin + m_readOffset;
		s.tail   = s.begin + m_writeOffset;
		s.Validate();
	}

	bool TcpRecvAssembler::EnsureWritable(uint32 bytes)
	{
		JAM_ASSERT(m_storage.IsValid());

		auto& s = m_storage.Get();
		const uint32 currentTailroom = s.end - (s.begin + m_writeOffset);

		if (currentTailroom >= bytes)
			return true;

		// 앞부분을 당기면 충분한지 먼저 확인
		if (m_readOffset > 0)
		{
			Compact();
			const uint32 tailroomAfterCompact = s.end - (s.begin + m_writeOffset);
			if (tailroomAfterCompact >= bytes)
				return true;
		}

		return Grow(bytes);
	}

	void TcpRecvAssembler::Compact()
	{
		JAM_ASSERT(m_storage.IsValid());

		const uint32 remain = BufferedBytes();
		auto& s = m_storage.Get();

		if (remain > 0 && m_readOffset > 0)
		{
			if (s.IsExclusive())
			{
				std::memmove(s.Begin(), s.Begin() + m_readOffset, remain);
			}
			else
			{
				BufWriter writer(m_pool);
				BufferSlice newSlice = writer.OpenForPayload(s.Capacity());
				std::memcpy(newSlice.Begin(), s.Begin() + m_readOffset, remain);
				newSlice.CommitReserved(s.Capacity());

				m_storage = MakeOwned(newSlice);
			}
		}

		m_readOffset  = 0;
		m_writeOffset = remain;
		SyncSliceWindow();
	}

	void TcpRecvAssembler::CompactIfNeeded()
	{
		// 다 소비했으면 cursor reset
		if (m_readOffset == m_writeOffset)
		{
			m_readOffset  = 0;
			m_writeOffset = 0;
			SyncSliceWindow();
			return;
		}

		// 앞쪽 낭비가 커지면 compact
		if (m_readOffset >= kCompactThreshold)
		{
			Compact();
		}
	}

	bool TcpRecvAssembler::Grow(uint32 additionalBytes)
	{
		JAM_ASSERT(m_storage.IsValid());

		const uint32 remain = BufferedBytes();
		const uint32 need = remain + additionalBytes;

		if (need > kHardMaxCapacity)
			return false;

		uint32 newCap = std::max<uint32>(m_storage->Capacity() * 2, need);
		newCap = std::min<uint32>(newCap, kHardMaxCapacity);

		if (newCap < need)
			return false;

		BufWriter writer(m_pool);
		BufferSlice newSlice = writer.OpenForPayload(newCap);

		if (remain > 0)
			std::memcpy(newSlice.Begin(), m_storage->Begin() + m_readOffset, remain);

		newSlice.CommitReserved(newCap);

		m_storage	  = MakeOwned(newSlice);
		m_readOffset  = 0;
		m_writeOffset = remain;
		SyncSliceWindow();
		return true;
	}

}
