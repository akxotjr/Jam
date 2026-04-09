#include "pch.h"
#include "jamnet/core/net/NetworkProfile.h"
#include "jamnet/core/net/SessionComponents.h"

#include <cmath>
#include <limits>


namespace jam::net::profile
{
	namespace
	{
		float BitsPerSecondToKbps(uint64 bytes, uint64 interval_ns)
		{
			if (interval_ns == 0)
				return 0.0f;

			const float bitsPerSecond = (static_cast<float>(bytes) * 8.0f * 1e9f) / static_cast<float>(interval_ns);
			return bitsPerSecond / 1000.0f;
		}

		float BitsPerSecondToMbps(uint64 bytes, uint64 interval_ns)
		{
			if (interval_ns == 0)
				return 0.0f;

			const float bitsPerSecond = (static_cast<float>(bytes) * 8.0f * 1e9f) / static_cast<float>(interval_ns);
			return bitsPerSecond / 1'000'000.0f;
		}

		float SafeRatioPct(uint64 num, uint64 den)
		{
			if (den == 0)
				return 0.0f;

			return (static_cast<float>(num) * 100.0f) / static_cast<float>(den);
		}

		float SafeRatio(uint64 num, uint64 den)
		{
			if (den == 0)
				return 0.0f;

			return static_cast<float>(num) / static_cast<float>(den);
		}

		float PerSecond(uint64 value, double seconds)
		{
			if (seconds <= 0.0)
				return 0.0f;

			return static_cast<float>(static_cast<double>(value) / seconds);
		}

		double NsToSeconds(uint64 duration_ns)
		{
			return static_cast<double>(duration_ns) / 1'000'000'000.0;
		}

		uint32 ClampToUint32(uint64 value)
		{
			return static_cast<uint32>(std::min<uint64>(value, std::numeric_limits<uint32>::max()));
		}

		template <size_t N>
		RudpLatencyStats BuildLatencyStatsFromRing(const std::array<uint64, N>& samples, uint16 count)
		{
			RudpLatencyStats stats{};
			if (count == 0)
				return stats;

			std::vector<uint64> values;
			values.reserve(count);
			for (uint16 i = 0; i < count; ++i)
				values.push_back(samples[i]);

			std::ranges::sort(values);

			double sum = 0.0;
			for (const auto value : values)
				sum += static_cast<double>(value);

			auto percentile = [&](float q)
				{
					const auto clamped = std::clamp(q, 0.0f, 1.0f);
					const auto idx = static_cast<size_t>(clamped * static_cast<float>(values.size() - 1));
					return values[idx];
				};

			stats.count  = count;
			stats.min_ns = values.front();
			stats.max_ns = values.back();
			stats.avg_ns = static_cast<uint64>(sum / static_cast<double>(values.size()));
			stats.p50_ns = percentile(0.50f);
			stats.p95_ns = percentile(0.95f);
			stats.p99_ns = percentile(0.99f);
			return stats;
		}

		RudpMetricsSnapshot CaptureRudpMetricsSnapshot(const RudpMetrics& metrics)
		{
			RudpMetricsSnapshot snapshot{};
			snapshot.txPackets					= metrics.txPackets;
			snapshot.txBytes					= metrics.txBytes;
			snapshot.rxPackets					= metrics.rxPackets;
			snapshot.rxBytes					= metrics.rxBytes;

			snapshot.reliableOriginalPackets	= metrics.reliableOriginalPackets;
			snapshot.reliableOriginalBytes		= metrics.reliableOriginalBytes;
			snapshot.reliableAckedPackets		= metrics.reliableAckedPackets;
			snapshot.reliableAckedBytes			= metrics.reliableAckedBytes;
			snapshot.firstSendAckedPackets		= metrics.firstSendAckedPackets;

			snapshot.rtxPackets					= metrics.rtxPackets;
			snapshot.rtxBytes					= metrics.rtxBytes;
			snapshot.rtxOriginalPackets			= metrics.rtxOriginalPackets;
			snapshot.rtxAckedPackets			= metrics.rtxAckedPackets;
			snapshot.rtxTimeoutPackets			= metrics.rtxTimeoutPackets;
			snapshot.rtxGiveupPackets			= metrics.rtxGiveupPackets;

			snapshot.pendingReliableNow			= metrics.pendingReliableNow;
			snapshot.pendingReliablePeak		= metrics.pendingReliablePeak;
			snapshot.maxRtxPerPacket			= metrics.maxRtxPerPacket;

			snapshot.outOfOrderPackets			= metrics.outOfOrderPackets;
			snapshot.duplicatePackets			= metrics.duplicatePackets;
			snapshot.appDeliveredPayloadBytes	= metrics.appDeliveredPayloadBytes;

			snapshot.fragOriginalPayloadBytes	= metrics.fragOriginalPayloadBytes;
			snapshot.fragWireBytes				= metrics.fragWireBytes;
			snapshot.fragReassemblyCompleted	= metrics.fragReassemblyCompleted;
			snapshot.fragReassemblyTimeoutDrops = metrics.fragReassemblyTimeoutDrops;

			snapshot.ackStandalonePackets		= metrics.ackStandalonePackets;
			snapshot.ackPiggybackedPackets		= metrics.ackPiggybackedPackets;

			snapshot.deliveryLatency = BuildLatencyStatsFromRing(metrics.deliveryLatencySamples_ns, metrics.deliveryLatencyCount);
			snapshot.recoveryLatency = BuildLatencyStatsFromRing(metrics.recoveryLatencySamples_ns, metrics.recoveryLatencyCount);
			return snapshot;
		}

		void ApplyLiveReliabilityState(entt::registry& R, entt::entity e, RudpMetricsSnapshot& snapshot)
		{
			const auto* reliability = R.try_get<ReliabilityState>(e);
			if (!reliability)
				return;

			const uint32 pendingNow = ClampToUint32(reliability->reliablePendings.size());
			snapshot.pendingReliableNow  = pendingNow;
			snapshot.pendingReliablePeak = std::max(snapshot.pendingReliablePeak, pendingNow);
		}

		uint64 GetConnectionDurationNs(entt::registry& R, entt::entity e)
		{
			const auto* info = R.try_get<SessionInfo>(e);
			if (!info || info->connectedTime_ns == 0)
				return 0;

			uint64 end_ns = NOW_NS();
			if (info->state == SessionInfo::DISCONNECTING || info->state == SessionInfo::DISCONNECTED)
				end_ns = std::max(info->lastRecvTime_ns, info->lastSendTime_ns);

			if (end_ns <= info->connectedTime_ns)
				return 0;

			return end_ns - info->connectedTime_ns;
		}

		RudpKpiView BuildRudpKpiView(const RudpMetricsSnapshot& snapshot, uint64 connectionDuration_ns)
		{
			const double connectedSeconds = NsToSeconds(connectionDuration_ns);

			RudpKpiView view{};
			view.txPacketsPerSec	 = PerSecond(snapshot.txPackets, connectedSeconds);
			view.rxPacketsPerSec	 = PerSecond(snapshot.rxPackets, connectedSeconds);
			view.txBytesPerSec		 = PerSecond(snapshot.txBytes, connectedSeconds);
			view.rxBytesPerSec		 = PerSecond(snapshot.rxBytes, connectedSeconds);

			view.avgTxBytesPerPacket = SafeRatio(snapshot.txBytes, snapshot.txPackets);
			view.avgRxBytesPerPacket = SafeRatio(snapshot.rxBytes, snapshot.rxPackets);

			view.firstSendSuccessPct = SafeRatioPct(snapshot.firstSendAckedPackets, snapshot.reliableOriginalPackets);
			view.rtxHitPct			 = SafeRatioPct(snapshot.rtxOriginalPackets, snapshot.reliableOriginalPackets);
			view.rtxRecoveryPct		 = SafeRatioPct(snapshot.rtxAckedPackets, snapshot.rtxOriginalPackets);
			view.avgRtxPerHitPacket  = SafeRatio(snapshot.rtxPackets, snapshot.rtxOriginalPackets);

			view.deliveryLatency	 = snapshot.deliveryLatency;
			view.recoveryLatency	 = snapshot.recoveryLatency;

			view.goodputPct			 = SafeRatioPct(snapshot.appDeliveredPayloadBytes, snapshot.rxBytes);
			view.fragEfficiencyPct	 = SafeRatioPct(snapshot.fragOriginalPayloadBytes, snapshot.fragWireBytes);
			view.ackPiggybackHitPct	 = SafeRatioPct(snapshot.ackPiggybackedPackets, snapshot.ackPiggybackedPackets + snapshot.ackStandalonePackets);

			view.outOfOrderPct		 = SafeRatioPct(snapshot.outOfOrderPackets, snapshot.rxPackets);
			view.duplicatePct		 = SafeRatioPct(snapshot.duplicatePackets, snapshot.rxPackets);

			view.pendingReliableNow	 = snapshot.pendingReliableNow;
			view.pendingReliablePeek = snapshot.pendingReliablePeak;
			view.maxRtxPerPacket	 = snapshot.maxRtxPerPacket;

			view.rtxTimeoutPct		 = SafeRatioPct(snapshot.rtxTimeoutPackets, snapshot.rtxPackets);
			view.rtxGiveupPct		 = SafeRatioPct(snapshot.rtxGiveupPackets, snapshot.rtxOriginalPackets);
			return view;
		}
	} // namespace

	void SessionTotalTraffic::OnRecv(uint32 bytes)
	{
		totalRecvBytes += bytes;
		totalRecvPackets++;
	}

	void SessionTotalTraffic::OnSend(uint32 bytes)
	{
		totalSendBytes += bytes;
		totalSendPackets++;
	}

	void SessionTotalTraffic::OnRecv(eChannelType ch, uint32 bytes)
	{
		OnRecv(bytes);
		channelTraffic[E2U(ch)].recvBytes += bytes;
		channelTraffic[E2U(ch)].recvPackets++;
	}

	void SessionTotalTraffic::OnSend(eChannelType ch, uint32 bytes)
	{
		OnSend(bytes);
		channelTraffic[E2U(ch)].sendBytes += bytes;
		channelTraffic[E2U(ch)].sendPackets++;
	}

	void LinkQualityState::AddWireRttSample(float rtt)
	{
		wireRttSamples[wireRttSampleIndex] = rtt;
		wireRttSampleIndex = (wireRttSampleIndex + 1) % 32;
		if (wireRttSampleCount < 32)
			wireRttSampleCount++;

		const bool hasPrev = (wireRttCumulativeCount > 0);
		const float prevRtt = wireRtt_ms;

		wireRtt_ms = rtt;
		wireRttMin_ms = std::min(wireRttMin_ms, rtt);
		wireRttMax_ms = std::max(wireRttMax_ms, rtt);

		wireRttCumulativeSum_ms += static_cast<double>(rtt);
		wireRttCumulativeCount++;
		wireRttCumulativeAvg_ms = static_cast<float>(wireRttCumulativeSum_ms / static_cast<double>(wireRttCumulativeCount));

		if (wireRttCumulativeCount == 1)
			wireRttAvg_ms = rtt;
		else
			wireRttAvg_ms += k_rttEwmaAlpha * (rtt - wireRttAvg_ms);

		if (wireRttSampleCount < 2)
		{
			wireJitter_ms = 0.0f;
			return;
		}

		if (!hasPrev)
		{
			wireJitter_ms = 0.0f;
		}
		else
		{
			const float delta = fabsf(rtt - prevRtt);
			wireJitter_ms = (1.0f - k_rttJitterBeta) * wireJitter_ms + k_rttJitterBeta * delta;
		}
	}

	void LinkQualityState::AddAppRttSample(float rtt)
	{
		appRttSamples[appRttSampleIndex] = rtt;
		appRttSampleIndex = (appRttSampleIndex + 1) % 32;
		if (appRttSampleCount < 32)
			appRttSampleCount++;

		const bool hasPrev = (appRttCumulativeCount > 0);
		const float prevRtt = appRtt_ms;

		appRtt_ms = rtt;
		appRttMin_ms = std::min(appRttMin_ms, rtt);
		appRttMax_ms = std::max(appRttMax_ms, rtt);

		appRttCumulativeSum_ms += static_cast<double>(rtt);
		appRttCumulativeCount++;
		appRttCumulativeAvg_ms = static_cast<float>(appRttCumulativeSum_ms / static_cast<double>(appRttCumulativeCount));

		if (appRttCumulativeCount == 1)
			appRttAvg_ms = rtt;
		else
			appRttAvg_ms += k_rttEwmaAlpha * (rtt - appRttAvg_ms);

		if (appRttSampleCount < 2)
		{
			appJitter_ms = 0.0f;
			return;
		}

		if (!hasPrev)
		{
			appJitter_ms = 0.0f;
		}
		else
		{
			const float delta = fabsf(rtt - prevRtt);
			appJitter_ms = (1.0f - k_rttJitterBeta) * appJitter_ms + k_rttJitterBeta * delta;
		}
	}

	void LinkQualityState::AccumulatePacketLoss(uint32 lost, uint32 expected)
	{
		lostPackets += lost;
		totalExpected += expected;
	}

	float LinkQualityState::GetPacketLoss() const
	{
		if (totalExpected == 0)
			return 0.0f;

		return static_cast<float>(lostPackets) / static_cast<float>(totalExpected);
	}

	void TrafficSampleState::RecordTrafficSample(uint64 recvBytes, uint64 sendBytes, uint64 interval_ns)
	{
		sampleRecvBytes   = recvBytes;
		sampleSendBytes   = sendBytes;
		sampleInterval_ns = interval_ns;
	}

	float TrafficSampleState::GetRecvThroughputKbps() const
	{
		return BitsPerSecondToKbps(sampleRecvBytes, sampleInterval_ns);
	}

	float TrafficSampleState::GetSendThroughputKbps() const
	{
		return BitsPerSecondToKbps(sampleSendBytes, sampleInterval_ns);
	}

	float TrafficSampleState::GetBandwidthMbps() const
	{
		return BitsPerSecondToMbps(sampleRecvBytes + sampleSendBytes, sampleInterval_ns);
	}

	NetworkStatsView NetworkStatsView::FromEntity(entt::registry& R, entt::entity e)
	{
		NetworkStatsView view{};
		bool hasLinkQuality = false;

		if (const auto* linkQuality = R.try_get<LinkQualityState>(e))
		{
			hasLinkQuality  = true;
			view.rtt_ms		= linkQuality->appRttAvg_ms;
			view.jitter_ms  = linkQuality->appJitter_ms;
			view.packetLoss = linkQuality->GetPacketLoss();
		}

		if (const auto* trafficSample = R.try_get<TrafficSampleState>(e))
		{
			view.recvThroughput_kbps = trafficSample->GetRecvThroughputKbps();
			view.sendThroughput_kbps = trafficSample->GetSendThroughputKbps();
			view.bandwidthMbps		 = trafficSample->GetBandwidthMbps();
		}

		const auto* metrics = R.try_get<RudpMetrics>(e);
		if (metrics)
		{
			const auto snapshot = CaptureRudpMetricsSnapshot(*metrics);
			const auto kpi = BuildRudpKpiView(snapshot, GetConnectionDurationNs(R, e));

			// Fallback to cumulative throughput when the latest short sample reports zero.
			if (view.recvThroughput_kbps <= 0.0f)
				view.recvThroughput_kbps = (kpi.rxBytesPerSec * 8.0f) / 1000.0f;

			if (view.sendThroughput_kbps <= 0.0f)
				view.sendThroughput_kbps = (kpi.txBytesPerSec * 8.0f) / 1000.0f;

			if (view.bandwidthMbps <= 0.0f)
				view.bandwidthMbps = ((kpi.rxBytesPerSec + kpi.txBytesPerSec) * 8.0f) / 1'000'000.0f;

			if (!hasLinkQuality)
				view.packetLoss = kpi.rtxHitPct / 100.0f;
		}

		return view;
	}

	RudpKpiView RudpKpiView::FromEntity(entt::registry& R, entt::entity e)
	{
		auto snapshot = RudpMetricsSnapshot::FromEntity(R, e);
		return BuildRudpKpiView(snapshot, GetConnectionDurationNs(R, e));
	}

	RudpMetricsSnapshot RudpMetricsSnapshot::FromEntity(entt::registry& R, entt::entity e)
	{
		const auto* metrics = R.try_get<RudpMetrics>(e);
		if (!metrics)
			return {};

		auto snapshot = CaptureRudpMetricsSnapshot(*metrics);
		ApplyLiveReliabilityState(R, e, snapshot);
		return snapshot;
	}

	void AccumulateSystemNetworkStats(
		TrafficSampleState& sampleState,
		const SessionTotalTraffic& totalTraffic,
		LinkQualityState* linkQuality,
		const RudpMetrics* metrics,
		uint64 interval_ns)
	{
		const uint64 sampleInterval_ns = (interval_ns > 0) ? interval_ns : 1_s;

		const uint64 recvNow   = totalTraffic.totalRecvBytes;
		const uint64 recvPrev  = sampleState.prevRecvBytes;
		const uint64 recvDelta = (recvNow >= recvPrev) ? (recvNow - recvPrev) : 0;
		sampleState.prevRecvBytes = recvNow;

		const uint64 sendNow   = totalTraffic.totalSendBytes;
		const uint64 sendPrev  = sampleState.prevSendBytes;
		const uint64 sendDelta = (sendNow >= sendPrev) ? (sendNow - sendPrev) : 0;
		sampleState.prevSendBytes = sendNow;

		sampleState.RecordTrafficSample(recvDelta, sendDelta, sampleInterval_ns);

		if (!linkQuality || !metrics)
			return;

		const uint64 txNow   = metrics->reliableOriginalPackets;
		const uint64 txPrev  = sampleState.prevTxPackets;
		const uint64 txDelta = (txNow >= txPrev) ? (txNow - txPrev) : 0;
		sampleState.prevTxPackets = txNow;

		const uint64 rtxNow   = metrics->rtxOriginalPackets;
		const uint64 rtxPrev  = sampleState.prevRetransmitPackets;
		const uint64 rtxDelta = (rtxNow >= rtxPrev) ? (rtxNow - rtxPrev) : 0;
		sampleState.prevRetransmitPackets = rtxNow;

		linkQuality->AccumulatePacketLoss(ClampToUint32(rtxDelta), ClampToUint32(txDelta));
	}

} // namespace jam::net::profile
