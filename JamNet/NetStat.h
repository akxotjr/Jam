#pragma once


namespace jam::net
{
	struct NetStat
	{
        double      rtt = 0.0;              // RTT ��հ� (tick)
        double      rttVariance = 0.0;      // RTT �л�, ���� ������ Ȱ��
        double      jitter = 0.0;           // ���� ������ (RTT ��ȭ��)
        double      margin = 0.5;
        int64       tickOffset = 0;
        float       packetLossSend = 0.0f;   // �۽� ���� �սǷ� (%)
        float       packetLossRecv = 0.0f;   // ���� ���� �սǷ� (%)
        float       bandwidthSend = 0.0f;    // �ʴ� �۽ŷ� (bytes/sec)
        float       bandwidthRecv = 0.0f;    // �ʴ� ���ŷ� (bytes/sec)
        uint64      totalSent = 0;
        uint64      totalRecv = 0;
        uint64      totalLost = 0;
        double      lastAckedTime = 0.0;
	};


    class NetStatTracker
    {
    public:
        NetStatTracker() = default;
        ~NetStatTracker() = default;

        // Server side
        void            OnRecvPing(uint64 clientSendTick, uint64 serverTick);

    	// Client side
    	void            OnRecvPong(uint64 clientSendTick, uint64 clientRecvTick, uint64 serverTick);


        void            OnSend(uint32 size);
        void            OnRecv(uint32 size);
        void            OnRecvAck(uint64 sequence);
        void            OnPacketLoss(uint32 count = 1);

        void            UpdateBandwidth(double deltaTime);


        double          GetRTT() { return m_netStat.rtt; }
        double          GetJitter() { return m_netStat.jitter; }

        const NetStat&  GetNetStat() { return m_netStat; }
        std::string     GetNetStatString() const;

        uint64          GetEstimatedClientTick(uint64 serverTick);
        uint64          GetEstimatedServerTick(uint64 clientTick);
        double          GetInterpolationDelayTick();

    private:
        double          EWMA(double prev, double cur, double alpha) const;

    private:
        NetStat         m_netStat = {};
        double          m_prevRtt = 0.0;
        uint64          m_bandwidthSendAccum = 0;
        uint64          m_bandwidthRecvAccum = 0;
        uint64          m_expectedRecvPackets = 0;
        uint64          m_highestSeqSeen = 0;
        uint64          m_lastAckedSeq = 0;
    };
}



