#pragma once

namespace jam::net
{
	constexpr float MTU = 1024;	// 1024 bytes

	class CongestionController
	{
	public:
		CongestionController() = default;
		~CongestionController() = default;


		void OnRecvAck(float rtt);

		void OnPacketLoss();

		bool CanSend(size_t inFlightBytes) const;

	private:
		float m_cwnd = 4 * MTU;
		float m_ssthresh = 32 * MTU;
		float m_smoothedRTT = 100.f;
		float m_latestRTT = 100.f;
	};
}

