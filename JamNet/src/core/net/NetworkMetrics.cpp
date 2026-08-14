#include "pch.h"
#include "jamnet/core/net/NetworkMetrics.h"

#include "jamnet/core/executor/GlobalExecutor.h"

namespace jam::net
{
	namespace
	{
		enum eNetworkValue : size_t
		{
			eTxBytes, 
			eRxBytes, 
			eTxPackets, 
			eRxPackets,
			eUdpTxBytes, 
			eUdpRxBytes, 
			eUdpTxPackets,
			eUdpRxPackets,
			eReliableOriginalBytes,
			eReliableOriginalPackets,
			eReliableAckedBytes, 
			eReliableAckedPackets,
			eRtxBytes, 
			eRtxPackets, 
			eRtxRecoveredPackets,
			eRtxTimeoutPackets, 
			eRtxGiveupPackets,
			eOutOfOrderPackets, 
			eDuplicatePackets,
			eAckStandalonePackets, 
			eAckPiggybackedPackets,
			ePendingReliableCurrent, 
			ePendingReliablePeak,
			eMaxRtxPerPacket, 
			eActiveSessions,
		};

		void Add(MetricSnapshot& snapshot, const size_t index, const uint64 value = 1)
		{
			snapshot.values[index].value += value;
		}
	}

	void NetworkMetrics::Init(const uint32 shardIndex)
	{
		m_shardIndex    = shardIndex;
		m_windowIndex   = UINT64_MAX;
		m_windowStartNs = 0;
		InitializeSnapshots();
	}

	void NetworkMetrics::RecordReceive(const uint64 bytes, const bool udp)
	{
		Add(m_shardSnapshot, eRxPackets);
		Add(m_shardSnapshot, eRxBytes, bytes);

		if (udp)
		{
			Add(m_shardSnapshot, eUdpRxPackets);
			Add(m_shardSnapshot, eUdpRxBytes, bytes);
		}
	}

	void NetworkMetrics::RecordTransmit(const uint64 bytes, const bool udp, const bool reliableOriginal,
		const bool retransmit, const bool standaloneAck, const bool piggybackedAck)
	{
		Add(m_shardSnapshot, eTxPackets);
		Add(m_shardSnapshot, eTxBytes, bytes);
		
		if (udp)
		{
			Add(m_shardSnapshot, eUdpTxPackets); 
			Add(m_shardSnapshot, eUdpTxBytes, bytes);
		}

		if (reliableOriginal)
		{
			Add(m_shardSnapshot, eReliableOriginalPackets); 
			Add(m_shardSnapshot, eReliableOriginalBytes, bytes);
		}
		
		if (retransmit)
		{
			Add(m_shardSnapshot, eRtxPackets); 
			Add(m_shardSnapshot, eRtxBytes, bytes);
		}

		if (standaloneAck) 
			Add(m_shardSnapshot, eAckStandalonePackets);
		
		if (piggybackedAck) 
			Add(m_shardSnapshot, eAckPiggybackedPackets);
	}

	void NetworkMetrics::RecordReliablePendingAdded()
	{
		auto& current = m_shardSnapshot.values[ePendingReliableCurrent].value;
		auto& peak    = m_shardSnapshot.values[ePendingReliablePeak].value;
		peak = std::max(peak, ++current);
	}

	void NetworkMetrics::RecordReliableAcked(const uint64 bytes, const bool recoveredByRetransmit)
	{
		Add(m_shardSnapshot, eReliableAckedPackets);
		Add(m_shardSnapshot, eReliableAckedBytes, bytes);
		if (recoveredByRetransmit) 
			Add(m_shardSnapshot, eRtxRecoveredPackets);
	}

	void NetworkMetrics::RemoveReliablePending(const uint64 count)
	{
		auto& current = m_shardSnapshot.values[ePendingReliableCurrent].value;
		current = count >= current ? 0 : current - count;
	}

	void NetworkMetrics::RecordRetransmitSubmitted(const uint64 retryCount)
	{
		auto& maximum = m_shardSnapshot.values[eMaxRtxPerPacket].value;
		maximum = std::max(maximum, retryCount);
	}

	void NetworkMetrics::RecordRetransmitTimeout()
	{
		Add(m_shardSnapshot, eRtxTimeoutPackets);
	}

	void NetworkMetrics::RecordRetransmitGiveup()
	{
		Add(m_shardSnapshot, eRtxGiveupPackets);
	}

	void NetworkMetrics::RecordOutOfOrder()
	{
		Add(m_shardSnapshot, eOutOfOrderPackets);
	}

	void NetworkMetrics::RecordDuplicate()
	{
		Add(m_shardSnapshot, eDuplicatePackets);
	}

	void NetworkMetrics::SetCurrent(const uint64 activeSessions, const uint64 pendingReliable)
	{
		m_shardSnapshot.values[eActiveSessions].value = activeSessions;
		m_shardSnapshot.values[ePendingReliableCurrent].value = pendingReliable;
		auto& peak = m_shardSnapshot.values[ePendingReliablePeak].value;
		peak = std::max(peak, pendingReliable);
	}

	void NetworkMetrics::UpdateWindow(const uint64 nowNs, MetricsAggregator& aggregator)
	{
		const uint64 windowIndex = aggregator.WindowIndex(nowNs);
		if (m_windowIndex == UINT64_MAX)
		{
			m_windowIndex = windowIndex;
			m_windowStartNs = windowIndex * aggregator.WindowPeriodNs();
			return;
		}
		if (m_windowIndex == windowIndex)
			return;

		const uint64 activeSessions = m_shardSnapshot.values[eActiveSessions].value;
		const uint64 pendingReliable = m_shardSnapshot.values[ePendingReliableCurrent].value;
		SubmitSnapshot(m_windowStartNs + aggregator.WindowPeriodNs());
		m_windowIndex = windowIndex;
		m_windowStartNs = windowIndex * aggregator.WindowPeriodNs();
		InitializeSnapshots();
		SetCurrent(activeSessions, pendingReliable);
	}

	void NetworkMetrics::SubmitSnapshot(const uint64 windowEndNs)
	{
		m_shardSnapshot.windowIndex   = m_windowIndex;
		m_shardSnapshot.windowStartNs = m_windowStartNs;
		m_shardSnapshot.windowEndNs   = windowEndNs;

		m_processSnapshot.windowIndex = m_windowIndex;
		m_processSnapshot.windowStartNs = m_windowStartNs;
		m_processSnapshot.windowEndNs = windowEndNs;

		for (size_t i = 0; i < m_shardSnapshot.values.size(); ++i)
			m_processSnapshot.values[i].value = m_shardSnapshot.values[i].value;

		GLOBAL_EXEC.SubmitMetrics(std::move(m_shardSnapshot));
		GLOBAL_EXEC.SubmitMetrics(std::move(m_processSnapshot));
	}

	void NetworkMetrics::InitializeSnapshots()
	{
		m_shardSnapshot = {};
		m_shardSnapshot.scope	   = "network_shard";
		m_shardSnapshot.shardIndex = m_shardIndex;
		m_shardSnapshot.values	   = {
			{ .name = "tx_bytes" },
			{ .name = "rx_bytes" },
			{ .name = "tx_packets" },
			{ .name = "rx_packets" },
			{ .name = "udp_tx_bytes" },
			{ .name = "udp_rx_bytes" },
			{ .name = "udp_tx_packets" },
			{ .name = "udp_rx_packets" },
			{ .name = "reliable_original_bytes" },
			{ .name = "reliable_original_packets" },
			{ .name = "reliable_acked_bytes" },
			{ .name = "reliable_acked_packets" },
			{ .name = "rtx_bytes" },
			{ .name = "rtx_packets" },
			{ .name = "rtx_recovered_packets" },
			{ .name = "rtx_timeout_packets" },
			{ .name = "rtx_giveup_packets" },
			{ .name = "out_of_order_packets" },
			{ .name = "duplicate_packets" },
			{ .name = "ack_standalone_packets" },
			{ .name = "ack_piggybacked_packets" },
			{ .name = "pending_reliable_current", .aggregation = eMetricAggregation::Latest },
			{ .name = "pending_reliable_peak", .aggregation = eMetricAggregation::Maximum },
			{ .name = "max_rtx_per_packet", .aggregation = eMetricAggregation::Maximum },
			{ .name = "active_sessions", .aggregation = eMetricAggregation::Latest },
		};

		m_processSnapshot = {};
		m_processSnapshot.scope = "network_process";
		m_processSnapshot.values = m_shardSnapshot.values;
		for (auto& value : m_processSnapshot.values)
		{
			if (value.aggregation == eMetricAggregation::Latest)
				value.aggregation = eMetricAggregation::Sum;
		}
		m_processSnapshot.values[ePendingReliablePeak].aggregation = eMetricAggregation::Sum;
	}
}
