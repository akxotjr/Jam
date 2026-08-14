#pragma once

#include "jamnet/core/utils/MetricsAggregator.h"

#include <array>

namespace jam::net
{
	class WorldMetrics
	{
	public:
		void			Init(uint64 worldId, uint32 shardIndex);

		void			RecordTick(const std::array<uint64, 6>& phasesNs, uint64 participantCount, uint64 nowNs);
		void			RecordAoiNeighbors(uint64 visibleActorCount);
		void			RecordAoiEnteredActor();
		void			RecordAoiLeftActor();
		void			RecordSnapshotPacket(uint64 bytes, uint64 actorCount, uint64 fullActorCount);
		void			RecordLifecyclePacket(uint64 bytes, uint64 eventCount);
		void			RecordLifecyclePacketSplit();
		void			SetLifecyclePending(uint64 pendingCount);
		void			RecordBaselineResend();
		void			RecordBaselineFullRequest();
		void			RecordBaselineResync();

		bool			IsReadyToSubmit(uint64 nowNs) const;

		void			Reset();

		void			SubmitSnapshot(uint64 nowNs);

	private:
		void			InitializeSnapshots();

	private:
		uint64			m_worldId = 0;
		uint32			m_shardIndex = 0;
		uint64			m_windowStartNs = 0;
		uint64			m_windowEndNs = 0;
		MetricSnapshot	m_simulationSnapshot;
		MetricSnapshot	m_aoiSnapshot;
		MetricSnapshot	m_replicationSnapshot;
	};
}
