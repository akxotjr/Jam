#include "pch.h"
#include "ReliableTransportManager.h"
#include "NetStatManager.h"
#include "Clock.h"

namespace jam::net
{
	void ReliableTransportManager::Update()
	{
		uint64 now = Clock::Instance().GetCurrentTick();

		if (m_pendingPackets.empty())
		{
			if (ShouldSendImmediateAck(now))
			{
				SendAck();
				ClearPendingAck();
			}
			return;
		}

		xvector<uint16> toRemove;
		xvector<uint16> toRetransmit;

		for (auto& [seq, pktInfo] : m_pendingPackets)
		{
			uint64 elapsed = now - pktInfo.timestamp;

			if (elapsed >= RETRANSMIT_TIMEOUT || pktInfo.retryCount >= MAX_RETRY_COUNT)
			{
				toRemove.push_back(seq);
				m_owner->GetNetStatTracker()->OnPacketLoss();
			}
			else if (elapsed >= RETRANSMIT_INTERVAL)
			{
				toRetransmit.push_back(seq);
			}
		}

		for (uint16 seq : toRemove)
		{
			auto it = m_pendingPackets.find(seq);
			if (it != m_pendingPackets.end())
			{
				m_inFlightSize -= it->second.size;
				m_pendingPackets.erase(it);
			}
		}

		// Retransmit
		for (uint16 seq : toRetransmit)
		{
			auto it = m_pendingPackets.find(seq);
			if (it != m_pendingPackets.end())
			{
				m_owner->SendDirect(it->second.buffer);
				it->second.retryCount++;
				it->second.timestamp = now;

				m_owner->GetNetStatTracker()->OnRetransmit();
			}
		}
	}

	void ReliableTransportManager::AddPendingPacket(uint16 seq, const Sptr<SendBuffer>& buf, uint64 timestamp)
	{
		m_pendingPackets[seq] = { buf, buf->WriteSize(), timestamp, 0 };
		m_inFlightSize += buf->WriteSize();
	}

	uint32 ReliableTransportManager::GenerateAckBitfield(uint16 latestSeq) const
	{
		uint32 bitfield = 0;
		for (uint16 i = 1; i <= BITFIELD_SIZE; ++i)
		{
			uint16 seq = latestSeq - i;

			if (!IsSeqGreator(latestSeq, seq))
				continue;

			if (m_receiveHistory.test(seq % WINDOW_SIZE))
			{
				bitfield |= (1 << (i - 1));
			}
		}
		return bitfield;
	}

	bool ReliableTransportManager::IsSeqReceived(uint16 seq)
	{
		if (!IsSeqGreator(seq, m_latestSeq - WINDOW_SIZE))
			return false;

		if (m_receiveHistory.test(seq % WINDOW_SIZE))
			return false;

		m_receiveHistory.set(seq % WINDOW_SIZE);
		m_latestSeq = IsSeqGreator(seq, m_latestSeq) ? seq : m_latestSeq;


		SetPendingAck(seq);

		return true;
	}

	void ReliableTransportManager::SendAck()
	{
		auto buf = PacketBuilder::CreateReliabilityAckPacket(m_pendingAckSeq, m_pendingAckBitfield);
		m_owner->SendDirect(buf);
	}

	void ReliableTransportManager::OnRecvAck(uint16 latestSeq, uint32 bitfield)
	{
		for (uint16 i = 0; i <= BITFIELD_SIZE; ++i)
		{
			uint16 ackSeq = latestSeq - i;
			if (i == 0 || (bitfield & (1 << (i - 1))))
			{
				auto it = m_pendingPackets.find(ackSeq);
				if (it != m_pendingPackets.end())
				{
					m_inFlightSize -= it->second.size;
					m_pendingPackets.erase(it);
				}
			}
		}
	}

	void ReliableTransportManager::SetPendingAck(uint16 seq)
	{
		uint64 now = Clock::Instance().GetCurrentTick();

		if (!m_hasPendingAck)
		{
			m_hasPendingAck = true;
			m_pendingAckSeq = seq;
			m_firstPendingAckTick = now;
		}
		else
		{
			if (IsSeqGreator(seq, m_pendingAckSeq))
			{
				m_pendingAckSeq = seq;
			}
		}

		m_pendingAckBitfield = GenerateAckBitfield(m_pendingAckSeq);
	}

	void ReliableTransportManager::ClearPendingAck()
	{
		m_hasPendingAck = false;
		m_pendingAckSeq = 0;
		m_pendingAckBitfield = 0;
		m_firstPendingAckTick = 0;
	}

	bool ReliableTransportManager::ShouldSendImmediateAck(uint64 currentTick)
	{
		return m_hasPendingAck && (currentTick - m_firstPendingAckTick) >= MAX_DELAY_TICK_PIGGYBACK_ACK;
	}

	bool ReliableTransportManager::TryAttachPiggybackAck(const Sptr<SendBuffer>& buf)
	{
		if (!m_hasPendingAck)
			return false;

		if (!buf || !buf->Buffer())
			return false;

		PacketHeader* pktHeader = reinterpret_cast<PacketHeader*>(buf->Buffer());
		uint16 currentSize = pktHeader->GetSize();
		uint32 remainingSpace = buf->AllocSize() - currentSize;

		if (remainingSpace < sizeof(AckHeader))
			return false;

		AckHeader* ackHeader = reinterpret_cast<AckHeader*>(buf->Buffer() + currentSize);
		ackHeader->latestSeq = m_pendingAckSeq;
		ackHeader->bitfield = m_pendingAckBitfield;

		uint16 newSize = currentSize + sizeof(AckHeader);
		uint8 newFlags = pktHeader->GetFlags() | PacketFlags::PIGGYBACK_ACK;
		pktHeader->SetSize(newSize);
		pktHeader->SetFlags(newFlags);

		buf->Close(newSize);
		ClearPendingAck();
		return true;
	}
	 
	void ReliableTransportManager::FailedAttachPiggybackAck()
	{
		uint64 now = Clock::Instance().GetCurrentTick();
		if (ShouldSendImmediateAck(now))
		{
			SendAck();
			ClearPendingAck();
		}
	}
}
