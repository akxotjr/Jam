#pragma once

#include "jamnet/core/utils/MetricsAggregator.h"

#include <array>

namespace jam::net
{
	class WorldMetrics
	{
	public:
		void			Init(uint64 worldId, uint32 shardIndex);

		void			RecordTick(const std::array<uint64, 6>& phasesNs, uint64 participantCount, Timestamp now);
		void			RecordAoiNeighbors(uint64 visibleActorCount);
		void			RecordAoiEnteredActor();
		void			RecordAoiLeftActor();
		void            RecordAoiEnqueueAttempt();
		void            RecordAoiUniqueRelation();
		void            RecordAoiCertificateEarlyReturn();
		void            RecordAoiCertificatePrecheck();
		void            RecordAoiCertificatePrecheckHit();
		void			RecordSnapshotPacket(uint64 bytes, uint64 actorCount, uint64 fullActorCount);
		void			RecordLifecyclePacket(uint64 bytes, uint64 eventCount);
		void			RecordLifecyclePacketSplit();
		void			SetLifecyclePending(uint64 pendingCount);
		void			RecordBaselineResend();
		void			RecordBaselineFullRequest();
		void			RecordBaselineResync();

		bool			IsReadyToSubmit(Timestamp nowNs) const;

		void			Reset();

		void			SubmitSnapshot(Timestamp nowNs);

	private:
		void			InitializeSnapshots();

	private:
		uint64			m_worldId = 0;
		uint32			m_shardIndex = 0;
		Timestamp		m_windowStart = Timestamp::Zero();
		Timestamp		m_windowEndNs   = Timestamp::Zero();
		MetricSnapshot	m_simulationSnapshot;
		MetricSnapshot	m_aoiSnapshot;
		MetricSnapshot	m_replicationSnapshot;
	};
}
