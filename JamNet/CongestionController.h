#pragma once

namespace jam::net
{
	class UdpSession;


	class CongestionController
	{
	public:
		CongestionController(UdpSession* owner) : m_owner(owner) {}
		~CongestionController() = default;

		void		OnRecvAck();
		void		OnPacketLoss();
		bool		CanSend(uint32 inFlightSize) const;
		uint32		GetCwnd() const { return m_cwnd; }


	private:
		UdpSession* m_owner = nullptr;
		uint32		m_cwnd = 4 * MTU;
		uint32		m_ssthresh = 32 * MTU;

		static constexpr uint32 MTU = 1024; // Maximum Transmission Unit for UDP
	};
}

  