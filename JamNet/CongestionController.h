#pragma once

namespace jam::net
{
	class UdpSession;


	constexpr uint32 MTU = 1024;		// Maximum Transmission Unit for UDP

	class CongestionController
	{
	public:
		CongestionController() = default;
		~CongestionController() = default;

		// Congestion Control Algorithm
		void OnRecvAck();
		void OnPacketLoss();
		bool CanSend(uint32 inFlightSize) const;
		uint32 GetCwnd() const { return m_cwnd; }


	private:
		Wptr<UdpSession> m_session; // Weak pointer to the session

		uint32 m_cwnd = 4 * MTU;
		uint32 m_ssthresh = 32 * MTU;
	};
}

  