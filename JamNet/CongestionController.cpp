#include "pch.h"
#include "CongestionController.h"

namespace jam::net
{
    void CongestionController::OnRecvAck()
    {
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

    bool CongestionController::CanSend(uint32 inFlightSize) const
    {
    	return inFlightSize < m_cwnd;
    }
}
