#include "pch.h"
#include "CongestionController.h"

namespace jam::net
{
    void CongestionController::OnRecvAck()
    {
        if (m_fastRecoveryMode) // Fast Recovery mode : increase cwnd per ACK (inflate)
        {
            m_cwnd += MTU;
        }
        else if (m_cwnd < m_ssthresh) 
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
        m_fastRecoveryMode = false;
    }

    void CongestionController::OnNewAck()
    {
        if (m_fastRecoveryMode)
        {
            m_cwnd = m_ssthresh;
            m_fastRecoveryMode = false;
        }
    }

    void CongestionController::OnFastRTX()
    {
        // Fast Recovery
        m_ssthresh = m_cwnd / 2;
        m_cwnd = m_ssthresh + 3 * MTU;
        m_fastRecoveryMode = true;
    }

    void CongestionController::OnNackRTX()
    {
        m_cwnd = max(MTU, m_cwnd * 0.9f);
    }

    bool CongestionController::CanSend(uint32 inFlightSize) const
    {
    	return inFlightSize < m_cwnd;
    }
}
