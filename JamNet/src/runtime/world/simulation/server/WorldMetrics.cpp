#include "pch.h"
#include "jamnet/runtime/world/simulation/server/WorldMetrics.h"

#include "jamnet/core/executor/GlobalExecutor.h"
#include "jamnet/runtime/world/simulation/common/WorldContext.h"

namespace jam::net
{
	namespace
	{
		constexpr uint64 kMetricsWindowNs = 5_s;
		const MetricHistogramConfig kDurationShortConfig{
			.lowestTrackableValue  = 1,
			.highestTrackableValue = 10'000'000,
			.significantDigits     = 3,
			.unit				   = "us",
		};

		enum eSimulationValue : size_t
		{
			eTickDurationOverflow,
			eTickOverrunCount,
			eParticipantsCurrent,
			eParticipantsPeak,
			eUsersCurrent,
		};

		enum eSimulationHistogram : size_t
		{
			eTickDuration,
			eInputDuration,
			ePhysicsDuration,
			eAoiDuration,
			eReplicationDuration,
			eFinalizeDuration,
		};

		enum eAoiValue : size_t
		{
			eNeighborsPerUserOverflow, 
			eEnteredActors, 
			eLeftActors
		};

		enum eReplicationValue : size_t
		{
			eSnapshotPackets, 
			eSnapshotChunks, 
			eSnapshotBytes, 
			eSnapshotActors, 
			eSnapshotFullActors, 
			eSnapshotDeltaActors,
			eLifecyclePackets, 
			eLifecycleBytes, 
			eLifecycleEvents, 
			eLifecyclePacketSplits,
			eLifecyclePendingCurrent,
			eLifecyclePendingPeak, 
			eBaselineResends, 
			eBaselineFullRequests, 
			eBaselineResyncs,
		};
	}

	void WorldMetrics::Init(uint64 worldId, uint32 shardIndex)
	{
		m_worldId = worldId;
		m_shardIndex = shardIndex;
		Reset();
	}

	void WorldMetrics::RecordTick(const std::array<uint64, 6>& phasesNs, uint64 participantCount, uint64 nowNs)
	{
		if (m_windowStartNs == 0)
			m_windowStartNs = nowNs;

		m_windowEndNs = nowNs;
		for (size_t i = 0; i < phasesNs.size(); ++i)
			m_simulationSnapshot.histograms[i].Record(phasesNs[i] / 1'000ull);

		if (phasesNs[0] > SIMULATION_TICK_NS)
			++m_simulationSnapshot.values[eTickOverrunCount].value;

		m_simulationSnapshot.values[eParticipantsCurrent].value = participantCount;
		m_simulationSnapshot.values[eParticipantsPeak].value = std::max(m_simulationSnapshot.values[eParticipantsPeak].value, participantCount);
		m_simulationSnapshot.values[eUsersCurrent].value = participantCount;
	}

	void WorldMetrics::RecordAoiNeighbors(uint64 visibleActorCount)
	{
		m_aoiSnapshot.histograms[0].Record(visibleActorCount);
	}

	void WorldMetrics::RecordAoiEnteredActor()
	{
		++m_aoiSnapshot.values[eEnteredActors].value;
	}

	void WorldMetrics::RecordAoiLeftActor()
	{
		++m_aoiSnapshot.values[eLeftActors].value;
	}

	void WorldMetrics::RecordSnapshotPacket(uint64 bytes, uint64 actorCount, uint64 fullActorCount)
	{
		++m_replicationSnapshot.values[eSnapshotPackets].value;
		++m_replicationSnapshot.values[eSnapshotChunks].value;
		m_replicationSnapshot.values[eSnapshotBytes].value += bytes;
		m_replicationSnapshot.values[eSnapshotActors].value += actorCount;
		m_replicationSnapshot.values[eSnapshotFullActors].value += fullActorCount;
		m_replicationSnapshot.values[eSnapshotDeltaActors].value += actorCount - fullActorCount;
	}

	void WorldMetrics::RecordLifecyclePacket(uint64 bytes, uint64 eventCount)
	{
		++m_replicationSnapshot.values[eLifecyclePackets].value;
		m_replicationSnapshot.values[eLifecycleBytes].value += bytes;
		m_replicationSnapshot.values[eLifecycleEvents].value += eventCount;
	}

	void WorldMetrics::RecordLifecyclePacketSplit()
	{
		++m_replicationSnapshot.values[eLifecyclePacketSplits].value;
	}

	void WorldMetrics::SetLifecyclePending(uint64 pendingCount)
	{
		m_replicationSnapshot.values[eLifecyclePendingCurrent].value = pendingCount;
		m_replicationSnapshot.values[eLifecyclePendingPeak].value = std::max(m_replicationSnapshot.values[eLifecyclePendingPeak].value, pendingCount);
	}

	void WorldMetrics::RecordBaselineResend()
	{
		++m_replicationSnapshot.values[eBaselineResends].value;
	}

	void WorldMetrics::RecordBaselineFullRequest()
	{
		++m_replicationSnapshot.values[eBaselineFullRequests].value;
	}

	void WorldMetrics::RecordBaselineResync()
	{
		++m_replicationSnapshot.values[eBaselineResyncs].value;
	}

	bool WorldMetrics::IsReadyToSubmit(uint64 nowNs) const
	{
		return m_windowStartNs != 0 && (nowNs - m_windowStartNs) >= kMetricsWindowNs;
	}

	void WorldMetrics::Reset()
	{
		m_windowStartNs = 0;
		m_windowEndNs = 0;
		InitializeSnapshots();
	}

	void WorldMetrics::SubmitSnapshot(uint64 nowNs)
	{
		if (auto* aggregator = GLOBAL_EXEC.GetMetricsAggregator(); aggregator && aggregator->IsEnabled())
		{
			const uint64 windowIndex = aggregator->WindowIndex(nowNs);

			m_simulationSnapshot.windowIndex   = windowIndex;
			m_simulationSnapshot.windowStartNs = m_windowStartNs;
			m_simulationSnapshot.windowEndNs   = m_windowEndNs;
			m_simulationSnapshot.values[eTickDurationOverflow].value = m_simulationSnapshot.histograms[eTickDuration].OverflowCount();
			GLOBAL_EXEC.SubmitMetrics(std::move(m_simulationSnapshot));

			m_aoiSnapshot.windowIndex   = windowIndex;
			m_aoiSnapshot.windowStartNs = m_windowStartNs;
			m_aoiSnapshot.windowEndNs   = m_windowEndNs;
			m_aoiSnapshot.values[eNeighborsPerUserOverflow].value = m_aoiSnapshot.histograms[0].OverflowCount();
			GLOBAL_EXEC.SubmitMetrics(std::move(m_aoiSnapshot));

			m_replicationSnapshot.windowIndex   = windowIndex;
			m_replicationSnapshot.windowStartNs = m_windowStartNs;
			m_replicationSnapshot.windowEndNs   = m_windowEndNs;
			GLOBAL_EXEC.SubmitMetrics(std::move(m_replicationSnapshot));
		}
	}

	void WorldMetrics::InitializeSnapshots()
	{
		m_simulationSnapshot = {};
		m_simulationSnapshot.scope = "simulation_world";
		m_simulationSnapshot.shardIndex = m_shardIndex;
		m_simulationSnapshot.sourceId = m_worldId;
		m_simulationSnapshot.values = {
			{ .name = "tick_duration_overflow" },
			{ .name = "tick_overrun_count" },
			{ .name = "participants_current", .aggregation = eMetricAggregation::Latest },
			{ .name = "participants_peak", .aggregation = eMetricAggregation::Maximum },
			{ .name = "users_current", .aggregation = eMetricAggregation::Latest },
		};
		m_simulationSnapshot.histograms.reserve(6);
		m_simulationSnapshot.histograms.emplace_back("tick_duration", kDurationShortConfig);
		m_simulationSnapshot.histograms.emplace_back("input_duration", kDurationShortConfig);
		m_simulationSnapshot.histograms.emplace_back("physics_duration", kDurationShortConfig);
		m_simulationSnapshot.histograms.emplace_back("aoi_duration", kDurationShortConfig);
		m_simulationSnapshot.histograms.emplace_back("replication_duration", kDurationShortConfig);
		m_simulationSnapshot.histograms.emplace_back("finalize_duration", kDurationShortConfig);

		m_aoiSnapshot = {};
		m_aoiSnapshot.scope		 = "aoi_world";
		m_aoiSnapshot.shardIndex = m_shardIndex;
		m_aoiSnapshot.sourceId   = m_worldId;
		m_aoiSnapshot.values	 = { 
			{ .name = "neighbors_per_user_overflow" }, 
			{ .name = "entered_actors" }, 
			{ .name = "left_actors" } 
		};
		m_aoiSnapshot.histograms.emplace_back("neighbors_per_user", 
			MetricHistogramConfig{ 
				.lowestTrackableValue  = 1, 
				.highestTrackableValue = 1'000'000, 
				.significantDigits	   = 2, 
				.unit				   = "actor" 
			});

		m_replicationSnapshot = {};
		m_replicationSnapshot.scope		 = "replication_world";
		m_replicationSnapshot.shardIndex = m_shardIndex;
		m_replicationSnapshot.sourceId	 = m_worldId;
		m_replicationSnapshot.values	 = {
			{ .name = "snapshot_packets" }, 
			{ .name = "snapshot_chunks" }, 
			{ .name = "snapshot_bytes" }, 
			{ .name = "snapshot_actors" },
			{ .name = "snapshot_full_actors" }, 
			{ .name = "snapshot_delta_actors" },
			{ .name = "lifecycle_packets" },
			{ .name = "lifecycle_bytes" }, 
			{ .name = "lifecycle_events" }, 
			{ .name = "lifecycle_packet_splits" },
			{ .name = "lifecycle_pending_current", .aggregation = eMetricAggregation::Latest }, 
			{ .name = "lifecycle_pending_peak", .aggregation = eMetricAggregation::Maximum },
			{ .name = "baseline_resends" },
			{ .name = "baseline_full_requests" },
			{ .name = "baseline_resyncs" },
		};
	}
}
