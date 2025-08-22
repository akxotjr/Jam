#include "pch.h"
#include "ReliableTransportManager.h"
#include "NetStatManager.h"
#include "CongestionController.h"
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
				m_owner->GetNetStatManager()->OnPacketLoss();
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

				m_owner->GetNetStatManager()->OnRTO();
				m_owner->GetCongestionController()->OnPacketLoss();
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

		//
		if (IsSeqGreator(seq, m_expectedNextSeq	))
		{
			if (ShouldSendNack(m_expectedNextSeq, seq))
			{
				uint32 nackBitfield = GenerateNackBitfield(m_expectedNextSeq);
				SendNack(m_expectedNextSeq, nackBitfield);
			}
		}

		m_receiveHistory.set(seq % WINDOW_SIZE);
		m_latestSeq = IsSeqGreator(seq, m_latestSeq) ? seq : m_latestSeq;

		//temp
		// 연속된 패킷만 expectedNextSeq 업데이트
		if (seq == m_expectedNextSeq)
		{
			m_expectedNextSeq++;
			// 연속된 다음 패킷들도 체크
			while (m_receiveHistory.test(m_expectedNextSeq % WINDOW_SIZE))
			{
				m_expectedNextSeq++;
			}
		}

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

					//
					m_duplicateAckCount.erase(ackSeq);
				}
			}
		}

		//
		CheckFastRTX(latestSeq);
		m_lastAckedSeq = latestSeq;
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

	void ReliableTransportManager::OnRecvNack(uint16 missingSeq, uint32 bitfield)
	{
		// NACK로 요청된 패킷들 즉시 재전송
		TriggerFastRTX(missingSeq);

		// 비트필드의 추가 누락 패킷들도 재전송
		for (uint16 i = 1; i <= 32; ++i)
		{
			if (bitfield & (1 << (i - 1)))
			{
				uint16 nackSeq = missingSeq + i;
				TriggerFastRTX(nackSeq);
			}
		}
	}

	void ReliableTransportManager::CheckFastRTX(uint16 ackSeq)
	{
		if (!IsSeqGreator(ackSeq, m_lastAckedSeq))  // 같거나 작은 경우
		{
			m_duplicateAckCount[ackSeq]++;

			if (m_duplicateAckCount[ackSeq] >= DUPLICATE_ACK_THRESHOLD)
			{
				uint16 missingSeq = ackSeq + 1;
				TriggerFastRTX(missingSeq);
				m_duplicateAckCount[ackSeq] = 0;
			}
		}
		else
		{
			// 새로운 ACK인 경우 중복 카운트 리셋
			m_duplicateAckCount.clear();
			m_lastAckedSeq = ackSeq;
		}
	}

	void ReliableTransportManager::TriggerFastRTX(uint16 seq)
	{
		auto it = m_pendingPackets.find(seq);
		if (it != m_pendingPackets.end())
		{
			// 즉시 재전송 (타임아웃 대기 없음)
			m_owner->SendDirect(it->second.buffer);
			it->second.retryCount++;
			it->second.timestamp = Clock::Instance().GetCurrentTick();

			m_owner->GetNetStatManager()->OnFastRTX(); // 통계 추가
			m_owner->GetCongestionController()->OnFastRTX(); // 혼잡 제어
		}
	}

	uint32 ReliableTransportManager::GenerateNackBitfield(uint16 missingSeq)
	{
		uint32 bitfield = 0;
		for (uint16 i = 1; i <= BITFIELD_SIZE; ++i)
		{
			uint16 seq = missingSeq + i;

			if (IsSeqGreator(seq, m_latestSeq))
				break;

			if (!m_receiveHistory.test(seq % WINDOW_SIZE))
			{
				bitfield |= (1 << (i - 1));
			}
		}
		return bitfield;
	}


	void ReliableTransportManager::SendNack(uint16 missingSeq, uint32 bitfield)
	{
		auto buf = PacketBuilder::CreateNackPacket(missingSeq, bitfield);
		m_owner->SendDirect(buf);
		m_lastNackTime = Clock::Instance().GetCurrentTick();
		m_sentNackSeqs.insert(missingSeq);
	}

	bool ReliableTransportManager::ShouldSendNack(uint16 expectedSeq, uint16 receivedSeq)
	{
		uint64 now = Clock::Instance().GetCurrentTick();

		// NACK 전송 간격 제한 (과도한 NACK 방지)
		if (now - m_lastNackTime < NACK_THROTTLE_INTERVAL)
			return false;

		if (m_sentNackSeqs.contains(expectedSeq))
			return false;

		// Gap이 일정 크기 이상일 때만 NACK 전송
		return IsSeqGreator(receivedSeq, expectedSeq + 1);
	}
}
