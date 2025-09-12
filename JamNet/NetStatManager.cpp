#include "pch.h"
#include "NetStatManager.h"
//#include "Clock.h"

namespace jam::net
{
	/*void NetStatManager::OnRecvPing(uint64 clientSendTick, uint64 serverRecvTick)
	{
		m_netStat.tickOffset = static_cast<int64>(serverRecvTick) - static_cast<int64>(clientSendTick);
	}

	void NetStatManager::OnRecvPong(uint64 clientSendTick, uint64 clientRecvTick, uint64 serverSendTick)
	{
		m_netStat.tickOffset = static_cast<int64>(serverSendTick) - static_cast<int64>(clientSendTick + (clientRecvTick - clientSendTick) / 2);
	}

	void NetStatManager::OnSend(uint32 size)
	{
		m_netStat.totalSent++;
		m_netStat.bandwidthSend += size;
	}

	void NetStatManager::OnRecv(uint32 size)
	{
		m_netStat.totalRecv++;
		m_netStat.bandwidthRecv += size;
	}

	void NetStatManager::OnSendReliablePacket()
	{
		m_prevTick = Clock::Instance().GetCurrentTick();
	}

	void NetStatManager::OnRecvAck(uint16 sequence)
	{
		uint64 now = Clock::Instance().GetCurrentTick();

		uint64 rtt = now - m_prevTick;
		m_netStat.rtt = EWMA(m_netStat.rtt, static_cast<double>(rtt), 0.1);
		m_netStat.jitter = EWMA(m_netStat.jitter, std::abs(static_cast<double>(rtt) - m_netStat.rtt), 0.1);

		if (m_netStat.rtt > 0)
		{
			double diff = m_netStat.rtt - m_prevRtt;
			m_netStat.rttVariance = EWMA(m_netStat.rttVariance, diff * diff, 0.1);
		}

		m_prevRtt = m_netStat.rtt;

		m_lastAckedSeq = sequence;
		if (sequence > m_highestSeqSeen)
		{
			m_expectedRecvPackets += (sequence - m_highestSeqSeen);
			m_highestSeqSeen = sequence;
		}

		m_netStat.totalAcksRecv++;
	}

	void NetStatManager::OnPacketLoss(uint32 count)
	{
		m_netStat.totalLost += count;
	}

	void NetStatManager::OnSendPiggybackAck()
	{
		m_netStat.piggybackAcks++;
		m_netStat.totalAcksSend++;
		m_ackSendAccum += sizeof(AckHeader);
	}

	void NetStatManager::OnSendImmediateAck()
	{
		m_netStat.immediateAcks++;
		m_netStat.totalAcksSend++;
		m_ackSendAccum += sizeof(PacketHeader) + sizeof(AckHeader);
	}

	void NetStatManager::OnSendDelayedAck()
	{
		m_netStat.delayedAcks++;
	}

	void NetStatManager::OnRetransmit()
	{
		m_netStat.totalLost++;
	}

	void NetStatManager::OnRTO()
	{
		m_netStat.timeoutRetransmits++;
		m_netStat.totalRetransmits++;
		
	}

	void NetStatManager::OnFastRTX()
	{
		m_netStat.fastRetransmits++;
		m_netStat.totalRetransmits++;
	}

	void NetStatManager::OnNackRTX()
	{
		m_netStat.nackRetransmits++;
		m_netStat.totalRetransmits++;
	}


	void NetStatManager::UpdateBandwidth()
	{
		m_netStat.bandwidthSend = static_cast<float>(m_bandwidthSendAccum / TICK_INTERVAL);
		m_netStat.bandwidthRecv = static_cast<float>(m_bandwidthRecvAccum / TICK_INTERVAL);

		m_bandwidthSendAccum = 0;
		m_bandwidthRecvAccum = 0;

		if (m_netStat.totalSent > 0)
		{
			m_netStat.packetLossSend = static_cast<float>(m_netStat.totalLost) / m_netStat.totalSent * 100.0f;
		}

		if (m_expectedRecvPackets > 0 && m_netStat.totalRecv > 0) 
		{
			m_netStat.packetLossRecv = static_cast<float>(m_expectedRecvPackets - m_netStat.totalRecv) / m_expectedRecvPackets * 100.0f;
			m_netStat.packetLossRecv = max(0.0f, min(100.0f, m_netStat.packetLossRecv));
		}
	}

	void NetStatManager::UpdateAckEfficiency()
	{
		if (m_bandwidthSendAccum > 0)
		{
			float  ackBandwidth = static_cast<float>(m_ackSendAccum / TICK_INTERVAL);
			m_netStat.ackEfficiency = ackBandwidth / m_netStat.bandwidthSend;
		}
		else
		{
			m_netStat.ackEfficiency = 0.0f;
		}

		m_ackSendAccum = 0;
	}

	void NetStatManager::UpdateRetransmitStats()
	{
		if (m_netStat.totalRetransmits > 0)
		{
			m_netStat.fastRetransmitRatio = static_cast<float>(m_netStat.fastRetransmits) / m_netStat.totalRetransmits;
		}
	}

	std::string NetStatManager::GetNetStatString() const
	{
		std::stringstream ss;
		ss << "RTT: " << m_netStat.rtt << "ms, ";
		ss << "Jitter: " << m_netStat.jitter << "ms, ";
		ss << "Packet Loss(Send): " << m_netStat.packetLossSend << "%, ";
		ss << "Packet Loss(Recv): " << m_netStat.packetLossRecv << "%, ";
		ss << "Bandwidth(Send): " << m_netStat.bandwidthSend / 1024.0f << "KB/s, ";
		ss << "Bandwidth(Recv): " << m_netStat.bandwidthRecv / 1024.0f << "KB/s, ";
		ss << "Total Sent: " << m_netStat.totalSent << ", ";
		ss << "Total Recv: " << m_netStat.totalRecv << ", ";
		ss << "Total Lost: " << m_netStat.totalLost;
		ss << "ACK Efficiency: " << m_netStat.ackEfficiency * 100.0f << "%, ";
		ss << "Total ACKs: " << m_netStat.totalAcksSend << " / " << m_netStat.totalAcksRecv;

		return ss.str();
	}

	uint64 NetStatManager::GetEstimatedClientTick(uint64 serverTick)
	{
		return serverTick + m_netStat.tickOffset;
	}

	uint64 NetStatManager::GetEstimatedServerTick(uint64 clientTick)
	{
		return clientTick + m_netStat.tickOffset;
	}

	double NetStatManager::GetInterpolationDelayTick()
	{
		return 1.0 + m_netStat.rtt * 0.5 + m_netStat.jitter + m_netStat.margin;
	}

	void NetStatManager::Update()
	{
		UpdateBandwidth();
		UpdateAckEfficiency();
		UpdateRetransmitStats();
	}


	double NetStatManager::EWMA(double prev, double cur, double alpha) const
	{
		return alpha * cur + (1 - alpha) * prev;
	}*/
}
