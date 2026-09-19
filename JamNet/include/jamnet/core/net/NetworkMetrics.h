#pragma once

#include "jamnet/core/utils/MetricsAggregator.h"

namespace jam::net
{
	class NetworkMetrics
	{
	public:
		void Init(uint32 shardIndex);

		void RecordReceive(uint64 bytes, bool udp);
		void RecordTransmit(uint64 bytes, bool udp, bool reliableOriginal, bool retransmit, bool standaloneAck, bool piggybackedAck);
		void RecordReliablePendingAdded();
		void RecordReliableAcked(uint64 bytes, bool recoveredByRetransmit);
		void RemoveReliablePending(uint64 count);
		void RecordRetransmitSubmitted(uint64 retryCount);
		void RecordRetransmitTimeout();
		void RecordRetransmitGiveup();
		void RecordOutOfOrder();
		void RecordDuplicate();
		void SetCurrent(uint64 activeSessions, uint64 pendingReliable);

		void UpdateWindow(Timestamp now, MetricsAggregator& aggregator);

	private:
		void InitializeSnapshots();
		void SubmitSnapshot(Timestamp windowEnd);

	private:
		uint32			m_shardIndex	= 0;
		uint64			m_windowIndex   = UINT64_MAX;
		Timestamp		m_windowStart = Timestamp::Zero();

		MetricSnapshot	m_shardSnapshot;
		MetricSnapshot	m_processSnapshot;
	};
}
