#pragma once


namespace jam::net
{
	class UdpSession;

	struct NetStat
	{
        double      rtt = 0.0;              // RTT 평균값 (tick)
        double      rttVariance = 0.0;      // RTT 분산, 지터 측정에 활용
        double      jitter = 0.0;           // 지터 추정값 (RTT 변화율)
        double      margin = 0.5;
        int64       tickOffset = 0;

        //float       packetLossSend = 0.0f;   // 송신 기준 손실률 (%)
        //float       packetLossRecv = 0.0f;   // 수신 기준 손실률 (%)

		float       bandwidthSend = 0.0f;    // 초당 송신량 (bytes/sec)
        float       bandwidthRecv = 0.0f;    // 초당 수신량 (bytes/sec)
        //uint64      totalSent = 0;
        //uint64      totalRecv = 0;
        //uint64      totalLost = 0;

        uint64      timeoutRetransmits = 0;
        uint64      fastRetransmits = 0;
        uint64      nackRetransmits = 0;
        uint64      totalRetransmits = 0;

        float       fastRetransmitRatio = 0.0f;
        double      avgRetransmitDelay = 0.0;

        uint64      totalAcksSend = 0;
        uint64      totalAcksRecv = 0;
        uint64      piggybackAcks = 0;
        uint64      immediateAcks = 0;
        uint64      delayedAcks = 0;
		float       ackEfficiency = 0.0f; // ACK 효율성 (송신된 ACK / 총 송신량)
	};

    struct ChannelStat
    {
        uint64      totalSent = 0;
        uint64      totalRecv = 0;
        uint64      totalLost = 0;   // only reliable channel
        uint64      totalDropped = 0;    // only sequenced channel
        uint64      totalBuffered = 0;   // only ordered channel
        uint64      totalReordered = 0;  // only ordered channel
        uint64      averageBufferDelay = 0;  //
        float       utilization = 0.0f;
    };


    class NetStatManager
    {
    public:
        NetStatManager() = default;
        ~NetStatManager() = default;

        // Server side
        void            OnRecvPing(uint64 clientSendTick, uint64 serverRecvTick);

    	// Client side
    	void            OnRecvPong(uint64 clientSendTick, uint64 clientRecvTick, uint64 serverSendTick);


        void            OnSend(uint32 size);
        void            OnRecv(uint32 size);

        void            OnSendReliablePacket(); 
        void            OnRecvAck(uint16 sequence);

        void            OnPacketLoss(uint32 count = 1);

        void            OnSendPiggybackAck();
        void            OnSendImmediateAck();
        void            OnSendDelayedAck();


        void            OnRetransmit();

        void            OnRTO();
        void            OnFastRTX();
        void            OnNackRTX();


        void            UpdateBandwidth();
        void            UpdateAckEfficiency();
        void            UpdateRetransmitStats();


        double          GetRTT() { return m_netStat.rtt; }
        double          GetJitter() { return m_netStat.jitter; }

        const NetStat&  GetNetStat() { return m_netStat; }
        std::string     GetNetStatString() const;

        uint64          GetEstimatedClientTick(uint64 serverTick);
        uint64          GetEstimatedServerTick(uint64 clientTick);
        double          GetInterpolationDelayTick();

        void            Update();


    private:
        double          EWMA(double prev, double cur, double alpha) const;

    private:
		Wptr<UdpSession>    m_session; 

        NetStat             m_netStat = {};
        uint64              m_prevTick = 0;
        double              m_prevRtt = 0.0;
        uint64              m_bandwidthSendAccum = 0;
        uint64              m_bandwidthRecvAccum = 0;
        uint64              m_expectedRecvPackets = 0;
        uint64              m_highestSeqSeen = 0;
        uint64              m_lastAckedSeq = 0;

		uint64              m_ackSendAccum = 0; // ACK 송신량 누적
    };
}



