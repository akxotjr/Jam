#pragma once

#include <array>
#include <cfloat>

#include "jamnet/core/net/PacketStructure.h"


namespace jam::net::profile
{


	struct ChannelTraffic
	{
		uint64 recvBytes	= 0;
		uint64 sendBytes	= 0;
		uint32 recvPackets	= 0;
		uint32 sendPackets	= 0;
	};

	struct SessionTotalTraffic
	{
		uint64							totalRecvBytes	 = 0;
		uint64							totalRecvPackets = 0;
		uint64							totalSendBytes	 = 0;
		uint64							totalSendPackets = 0;
		std::array<ChannelTraffic, 4>	channelTraffic	 = {};

		void							OnRecv(uint32 bytes);
		void							OnSend(uint32 bytes);
		void							OnRecv(eChannelType ch, uint32 bytes);
		void							OnSend(eChannelType ch, uint32 bytes);
	};

	struct LinkQualityState
	{
		static constexpr float		k_rttEwmaAlpha			= 0.125f;
		static constexpr float		k_rttJitterBeta			= 0.25f;

		// Wire RTT (IOCP ingress / egress)
		float						wireRtt_ms				= 0.0f;
		float						wireRttMin_ms			= FLT_MAX;
		float						wireRttMax_ms			= 0.0f;
		float						wireRttAvg_ms			= 0.0f;
		float						wireRttCumulativeAvg_ms = 0.0f;
		float						wireJitter_ms			= 0.0f;

		double						wireRttCumulativeSum_ms = 0.0;
		uint64						wireRttCumulativeCount  = 0;
		std::array<float, 32>		wireRttSamples			= {};
		uint8						wireRttSampleIndex		= 0;
		uint8						wireRttSampleCount		= 0;

		// Application RTT
		float						appRtt_ms				= 0.0f;
		float						appRttMin_ms			= FLT_MAX;
		float						appRttMax_ms			= 0.0f;
		float						appRttAvg_ms			= 0.0f;
		float						appRttCumulativeAvg_ms  = 0.0f;
		float						appJitter_ms			= 0.0f;

		double						appRttCumulativeSum_ms	= 0.0;
		uint64						appRttCumulativeCount	= 0;
		std::array<float, 32>		appRttSamples			= {};
		uint8						appRttSampleIndex		= 0;
		uint8						appRttSampleCount		= 0;

		uint32						lostPackets				= 0;
		uint32						totalExpected			= 0;

		void						AddWireRttSample(float rtt);
		void						AddAppRttSample(float rtt);
		void						AccumulatePacketLoss(uint32 lost, uint32 expected);
		float						GetPacketLoss() const;
	};

	struct TrafficSampleState
	{
		uint64						prevRecvBytes			= 0;
		uint64						prevSendBytes			= 0;
		uint64						prevTxPackets			= 0;
		uint64						prevRetransmitPackets	= 0;

		uint64						sampleRecvBytes			= 0;
		uint64						sampleSendBytes			= 0;
		uint64						sampleInterval_ns		= 0;

		void						RecordTrafficSample(uint64 recvBytes, uint64 sendBytes, uint64 interval_ns);
		float						GetRecvThroughputKbps() const;
		float						GetSendThroughputKbps() const;
		float						GetBandwidthMbps() const;
	};

	struct RudpMetrics
	{
		// send / recv
		uint64					txPackets					= 0;
		uint64					txBytes						= 0;
		uint64					rxPackets					= 0;
		uint64					rxBytes						= 0;

		// reliable originals only (exclude retransmits)
		uint64					reliableOriginalPackets		= 0;
		uint64					reliableOriginalBytes		= 0;

		// reliable final delivery
		uint64					reliableAckedPackets		= 0;
		uint64					reliableAckedBytes			= 0;

		// first-send success
		uint64					firstSendAckedPackets		= 0;

		// retransmit
		uint64					rtxPackets					= 0;
		uint64					rtxBytes					= 0;
		uint64					rtxOriginalPackets			= 0;
		uint64					rtxAckedPackets				= 0;
		uint64					rtxTimeoutPackets			= 0;
		uint64					rtxGiveupPackets			= 0;

		uint32					pendingReliableNow			= 0;
		uint32					pendingReliablePeak			= 0;
		uint32					maxRtxPerPacket				= 0;

		std::array<uint64, 128> deliveryLatencySamples_ns	= {};
		uint16					deliveryLatencyHead			= 0;
		uint16					deliveryLatencyCount		= 0;

		std::array<uint64, 128> recoveryLatencySamples_ns	= {};
		uint16					recoveryLatencyHead			= 0;
		uint16					recoveryLatencyCount		= 0;

		uint64					outOfOrderPackets			= 0;
		uint64					duplicatePackets			= 0;
		uint64					appDeliveredPayloadBytes	= 0;

		uint64					fragOriginalPayloadBytes	= 0;
		uint64					fragWireBytes				= 0;
		uint64					fragReassemblyCompleted		= 0;
		uint64					fragReassemblyTimeoutDrops	= 0;

		uint64					ackStandalonePackets		= 0;
		uint64					ackPiggybackedPackets		= 0;


		void AddDeliveryLatency(uint64 latency_ns)
		{
			deliveryLatencySamples_ns[deliveryLatencyHead] = latency_ns;
			deliveryLatencyHead = static_cast<uint16>((deliveryLatencyHead + 1) % deliveryLatencySamples_ns.size());
			if (deliveryLatencyCount < deliveryLatencySamples_ns.size())
				deliveryLatencyCount++;
		}

		void AddRecoveryLatency(uint64 latency_ns)
		{
			recoveryLatencySamples_ns[recoveryLatencyHead] = latency_ns;
			recoveryLatencyHead = static_cast<uint16>((recoveryLatencyHead + 1) % recoveryLatencySamples_ns.size());
			if (recoveryLatencyCount < recoveryLatencySamples_ns.size())
				++recoveryLatencyCount;
		}
	};

	struct NetworkStatsView
	{
		float rtt_ms				= 0.0f;
		float jitter_ms				= 0.0f;
		float packetLoss			= 0.0f;
		float recvThroughput_kbps	= 0.0f;
		float sendThroughput_kbps	= 0.0f;
		float bandwidthMbps			= 0.0f;

		static NetworkStatsView FromEntity(entt::registry& R, entt::entity e);
	};

	struct RudpLatencyStats
	{
		uint32				count  = 0;
		uint64				min_ns = 0;
		uint64				max_ns = 0;
		uint64				avg_ns = 0;
		uint64				p50_ns = 0;
		uint64				p95_ns = 0;
		uint64				p99_ns = 0;
	};

	struct RudpKpiView
	{
		float				txPacketsPerSec		= 0.0;
		float				rxPacketsPerSec		= 0.0;
		float				txBytesPerSec		= 0.0;
		float				rxBytesPerSec		= 0.0;
		
		float				avgTxBytesPerPacket = 0.0;
		float				avgRxBytesPerPacket = 0.0;

		float				firstSendSuccessPct = 0.0;
		float				rtxHitPct			= 0.0f;
		float				rtxRecoveryPct		= 0.0f;
		float				avgRtxPerHitPacket  = 0.0f;

		RudpLatencyStats	deliveryLatency		= {};
		RudpLatencyStats	recoveryLatency		= {};

		float				goodputPct			= 0.0f;
		float				fragEfficiencyPct	= 0.0f;
		float				ackPiggybackHitPct	= 0.0f;

		float				outOfOrderPct		= 0.0f;
		float				duplicatePct		= 0.0f;

		uint32				pendingReliableNow	= 0;
		uint32				pendingReliablePeek = 0;
		uint32				maxRtxPerPacket		= 0;

		float				rtxTimeoutPct		= 0.0f;
		float				rtxGiveupPct		= 0.0f;

		static RudpKpiView FromEntity(entt::registry& R, entt::entity e);
	};

	struct RudpMetricsSnapshot
	{
		uint64				txPackets					= 0;
		uint64				txBytes						= 0;
		uint64				rxPackets					= 0;
		uint64				rxBytes						= 0;

		uint64				reliableOriginalPackets		= 0;
		uint64				reliableOriginalBytes		= 0;
		uint64				reliableAckedPackets		= 0;
		uint64				reliableAckedBytes			= 0;
		uint64				firstSendAckedPackets		= 0;

		uint64				rtxPackets					= 0;
		uint64				rtxBytes					= 0;
		uint64				rtxOriginalPackets			= 0;
		uint64				rtxAckedPackets				= 0;
		uint64				rtxTimeoutPackets			= 0;
		uint64				rtxGiveupPackets			= 0;

		uint32				pendingReliableNow			= 0;
		uint32				pendingReliablePeak			= 0;
		uint32				maxRtxPerPacket				= 0;

		uint64				outOfOrderPackets			= 0;
		uint64				duplicatePackets			= 0;
		uint64				appDeliveredPayloadBytes	= 0;

		uint64				fragOriginalPayloadBytes	= 0;
		uint64				fragWireBytes				= 0;
		uint64				fragReassemblyCompleted		= 0;
		uint64				fragReassemblyTimeoutDrops	= 0;

		uint64				ackStandalonePackets		= 0;
		uint64				ackPiggybackedPackets		= 0;

		RudpLatencyStats	deliveryLatency				= {};
		RudpLatencyStats	recoveryLatency				= {};

		static RudpMetricsSnapshot FromEntity(entt::registry& R, entt::entity e);
	};

	void AccumulateSystemNetworkStats(TrafficSampleState& sampleState, const SessionTotalTraffic& totalTraffic, LinkQualityState* linkQuality, const RudpMetrics* metrics, uint64 interval_ns);


} // namespace jam::net::profile
