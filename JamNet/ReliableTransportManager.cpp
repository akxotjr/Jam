#include "pch.h"
#include "ReliableTransportManager.h"

namespace jam::net
{
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
		return true;
	}

	xvector<uint16> ReliableTransportManager::GetPendingPacketsToRetransmit(uint64 currentTick) const
	{
		xvector<uint16> resendList;
		for (const auto& [seq, pktInfo] : m_pendingPackets)
		{
			if (pktInfo.retryCount < MAX_RETRY_COUNT && currentTick - pktInfo.timestamp >= RETRANSMIT_INTERVAL)
			{
				resendList.push_back(seq);
			}
		}
		return resendList;
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
}
