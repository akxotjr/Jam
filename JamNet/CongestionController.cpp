#include "pch.h"
#include "CongestionController.h"

namespace jam::net
{
    void CongestionController::OnRecvAck(float rtt)
    {
        m_latestRTT = rtt;
        m_smoothedRTT = 0.875f * m_smoothedRTT + 0.125f * rtt;

        if (m_cwnd < m_ssthresh) 
        {
            m_cwnd += MTU; // Slow Start
        }
        else 
        {
            m_cwnd += MTU * (MTU / m_cwnd); // Congestion Avoidance
        }
    }

    void CongestionController::OnPacketLoss()
    {
        m_ssthresh = m_cwnd / 2;
        m_cwnd = MTU; // Fast Retransmit or Timeout
    }

    bool CongestionController::CanSend(size_t inFlightBytes) const
    {
    	return inFlightBytes < m_cwnd;
    }
}
