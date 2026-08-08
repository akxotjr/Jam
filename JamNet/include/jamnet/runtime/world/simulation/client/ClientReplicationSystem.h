#pragma once
#include "jamnet/runtime/world/simulation/common/ReplicationTypes.h"
#include "jamnet/runtime/protocol/schema/gen/lifecycle_generated.h"
#include "jamnet/runtime/protocol/schema/gen/snapshot_generated.h"
#include "jamnet/runtime/protocol/schema/gen/baseline_ack_generated.h"

#include <map>
#include <optional>
#include <unordered_map>

namespace jam::net
{
    class ClientWorld;
    class ClientPhysicsSystem;
    struct EstimatedServerTick;
    struct LocalActorRef;
    struct ReconcileSignal;

    struct Replica
    {
        ActorId           actorId           = ActorId::Invalid();
        entt::entity    e               = entt::null;
        uint64          lastSeenTick    = 0;
        uint32          baselineRev     = 0;
        bool            isLocal         = false;

        bool            hasBaseline     = false;
        px::Vec3        baselinePos     = px::Vec3::Zero();
        px::Quat        baselineRot     = px::Quat::Identity();
		float           baselineBodyYaw = 0.0f;
		float           baselineViewYaw = 0.0f;
        float           baselinePitch   = 0.0f;

        bool            spawnEventFired = false;
    };

    struct PendingLifecycleBatch
    {
        fb::fbLifecycleBatchT batch = {};
    };

    struct PendingSnapshot
    {
        fb::fbSnapshotT snapshot = {};
        uint64          recvNs   = 0;
    };

    struct PendingSnapshotBatch
    {
        uint64                          serverTick = 0;
        uint32                          inputAck = 0;
        uint32                          inputEpoch = 0;
        uint16                          expectedChunkCount = 1;
        uint64                          firstRecvNs = 0;
        uint64                          lastRecvNs = 0;
        std::vector<std::optional<PendingSnapshot>> chunks;

        bool IsComplete() const
        {
            if (expectedChunkCount == 0 || chunks.size() < expectedChunkCount)
                return false;

            for (uint16 i = 0; i < expectedChunkCount; ++i)
            {
                if (!chunks[i].has_value())
                    return false;
            }
            return true;
        }

        uint16 ReceivedChunkCount() const
        {
            const uint16 maxCount = std::min<uint16>(expectedChunkCount, static_cast<uint16>(chunks.size()));
            uint16 count = 0;
            for (uint16 i = 0; i < maxCount; ++i)
            {
                if (chunks[i].has_value())
                    ++count;
            }
            return count;
        }

        bool ContainsActorId(uint32 rawActorId) const
        {
            for (const auto& chunk : chunks)
            {
                if (!chunk.has_value())
                    continue;

                for (const auto& entPtr : chunk->snapshot.entities)
                {
                    if (entPtr && entPtr->actor_id == rawActorId)
                        return true;
                }
            }
            return false;
        }
    };

    struct DeferredBaselineSnapshot
    {
        fb::fbActorEntityT entity = {};
        uint64             serverTick = 0;
        uint32             inputAck = 0;
        uint32             inputEpoch = 0;
    };

	class ClientReplicationSystem
	{
    public:
        explicit ClientReplicationSystem(entt::registry& world);

        void                                Init();
        void                                Clear();
        void                                Tick();

        void                                EnqueueLifecycle(fb::fbLifecycleBatchT batch);
        void                                EnqueueSnapshot(fb::fbSnapshotT snapshot, uint64 recvNs);

        bool                                IsLocalActor(ActorId actorId) const { return actorId == m_localActorId; }

        uint64                              GetLastServerTick() const { return m_lastServerTick; }
        uint32                              GetLastInputAck() const { return m_lastInputAck; }

        ActorId                               GetLocalActorId() const { return m_localActorId; }
        void                                SetLocalActorId(ActorId actorId) { m_localActorId = actorId; }
        entt::entity                        GetLocalEntity() const { return m_localEntity; }

    private:
        void                                ProcessLifecycleActor(const fb::fbLifecycleActorT& actor);
        void                                ApplyActorMeta(ActorId actorId, entt::entity entity, const fb::fbActorMetaT& meta, Replica& replica);
        void                                ProcessEntity(const fb::fbActorEntityT& ent, uint64 serverTick, uint32 inputAck, uint32 inputEpoch);
        entt::entity                        ResolveEntityForSnapshot(ActorId actorId);
        void                                PreserveDeferredBaselineSnapshots(const PendingSnapshotBatch& batch);
        void                                StoreDeferredBaselineSnapshot(const fb::fbActorEntityT& ent, uint64 serverTick, uint32 inputAck, uint32 inputEpoch);
        void                                ApplyDeferredBaselineSnapshot(ActorId actorId);
        bool                                HasBaselinePayload(const fb::fbActorEntityT& ent) const;
        bool                                NeedsBaseline(ActorId actorId) const;
        
		void                                ApplyRigidFullSnapshot(Replica& replica, uint64 serverTick, const fb::fbTransformFull* tf, uint32 baselineRev);
        void                                ApplyRigidDeltaSnapshot(Replica& replica, uint64 serverTick, const fb::fbTransformDelta* tf, uint32 baselineRev);
        void                                ApplyKinematicStateSnapshot(Replica& replica, uint64 serverTick, const fb::fbKinematicState* ks, uint32 baselineRev);

        void                                ApplyCharacterFullSnapshot(Replica& replica, uint64 serverTick, const fb::fbCharacterFull160* ch, uint32 baselineRev, uint32 inputAck, uint32 inputEpoch);
        void                                ApplyCharacterDeltaSnapshot(Replica& replica, uint64 serverTick, const fb::fbCharacterDelta128* ch, uint32 baselineRev, uint32 inputAck, uint32 inputEpoch);

		Replica&                            GetOrCreateReplica(ActorId actorId, bool* created = nullptr);
        void                                PruneOldReplicas(uint64 serverTick, uint64 forgetAfterTicks = 300);
        void                                UpdateUniqueLocalFromMeta(ActorId actorId, const fb::fbActorMetaT& meta, Replica& replica);

        void                                ResolveDeferredTargetBindingsAndSpawn();
        bool                                TryResolveTargetActorId(ActorId targetActorId, OUT px::ActorId& outActorId);
		uint32                              GetCurrentLocalControlRevision() const;
        void                                SetLocalActorRef(ActorId actorId, entt::entity entity);
        void                                ClearLocalActorRef();
		void                                QueueBaselineAck(ActorId actorId, uint32 baselineRev);
		void                                QueueFullRequest(ActorId actorId, uint32 baselineRev);
		void                                FlushBaselineFeedback();

    private:
        entt::registry&                     m_world;
        ClientWorld*                m_netWorld            = nullptr;
        ClientPhysicsSystem*                m_clientPhysics       = nullptr;
        EstimatedServerTick*                m_estimatedServerTick = nullptr;
        LocalActorRef*                      m_localActorRef       = nullptr;
        ReconcileSignal*                    m_reconcileSignal     = nullptr;

        uint64                              m_userId            = 0;

        std::unordered_map<ActorId, Replica>  m_replicas;
        std::unordered_map<ActorId, DeferredBaselineSnapshot> m_deferredBaselineSnapshots;
        std::deque<PendingLifecycleBatch>   m_pendingLifecycle;
        std::map<uint64, PendingSnapshotBatch> m_pendingSnapshotBatches;

        ActorId                               m_localActorId        = ActorId::Invalid();
        entt::entity                        m_localEntity       = entt::null;

		uint64                              m_lastServerTick    = 0;
        uint64                              m_lastLifecycleTick = 0;
        uint64                              m_latestQueuedSnapshotTick = 0;
        uint64                              m_lastAppliedSnapshotTick = 0;
        uint32                              m_lastInputAck      = 0;
		struct PendingBaselineFeedback
		{
			uint32 baselineRev = 0;
			bool requestFull = false;
		};
		std::unordered_map<ActorId, PendingBaselineFeedback> m_pendingBaselineFeedback;
	};
}
