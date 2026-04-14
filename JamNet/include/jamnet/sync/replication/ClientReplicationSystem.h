#pragma once
#include "jamnet/sync/replication/ReplicationTypes.h"
#include "jamnet/sync/schema/gen/lifecycle_generated.h"
#include "jamnet/sync/schema/gen/snapshot_generated.h"

namespace jam::net
{
    struct Replica
    {
        NetId           netId           = NetId::Invalid();
        entt::entity    e               = entt::null;
        uint64          lastSeenTick    = 0;
        uint32          baselineRev     = 0;
        bool            isLocal         = false;

        bool            hasBaseline     = false;
        px::Vec3        baselinePos     = px::Vec3::Zero();
        px::Quat        baselineRot     = px::Quat::Identity();
        float           baselineYaw     = 0.0f;
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

	class ClientReplicationSystem
	{
    public:
        explicit ClientReplicationSystem(entt::registry& world);

        void                                Init();
        void                                Clear();
        void                                Tick();

        void                                EnqueueLifecycle(fb::fbLifecycleBatchT batch);
        void                                EnqueueSnapshot(fb::fbSnapshotT snapshot, uint64 recvNs);

        bool                                IsLocalActor(NetId netId) const { return netId == m_localNetId; }

        uint64                              GetLastServerTick() const { return m_lastServerTick; }
        uint32                              GetLastInputAck() const { return m_lastInputAck; }

        NetId                               GetLocalNetId() const { return m_localNetId; }
        void                                SetLocalNetId(NetId netId) { m_localNetId = netId; }
        entt::entity                        GetLocalEntity() const { return m_localEntity; }

    private:
        void                                ProcessLifecycleActor(const fb::fbLifecycleActorT& actor);
        void                                ApplyActorMeta(NetId netId, entt::entity entity, const fb::fbActorMetaT& meta, Replica& replica);
        void                                ProcessEntity(const fb::fbActorEntityT& ent, uint64 serverTick, uint32 inputEpoch);
        entt::entity                        ResolveEntityForSnapshot(NetId netId);
        
		void                                ApplyRigidFullSnapshot(uint64 serverTick, NetId netId, const fb::fbTransformFull* tf, uint32 baselineRev);
        void                                ApplyRigidDeltaSnapshot(uint64 serverTick, NetId netId, const fb::fbTransformDelta* tf, uint32 baselineRev);
        void                                ApplyKinematicStateSnapshot(uint64 serverTick, NetId netId, const fb::fbKinematicState* ks, uint32 baselineRev);

        void                                ApplyCharacterFullSnapshot(uint64 serverTick, NetId netId, const fb::fbCharacterFull160* ch, uint32 baselineRev, bool isLocal, uint32 inputEpoch);
        void                                ApplyCharacterDeltaSnapshot(uint64 serverTick, NetId netId, const fb::fbCharacterDelta128* ch, uint32 baselineRev, bool isLocal, uint32 inputEpoch);

		Replica&                            GetOrCreateReplica(NetId netId, bool* created = nullptr);
        void                                PruneOldReplicas(uint64 serverTick, uint64 forgetAfterTicks = 300);
        void                                UpdateUniqueLocalFromMeta(NetId netId, const fb::fbActorMetaT& meta, Replica& replica);

        void                                ResolveDeferredTargetBindingsAndSpawn();
        bool                                TryResolveTargetObjId(NetId targetNetId, OUT px::ObjectId& outObjId);
        uint32                              GetCurrentLocalCommandEpoch() const;
        void                                SetLocalActorRef(NetId netId, entt::entity entity);
        void                                ClearLocalActorRef();

    private:
        entt::registry&                     m_world;

        uint64                              m_userId            = 0;

        std::unordered_map<NetId, Replica>  m_replicas;
        std::deque<PendingLifecycleBatch>   m_pendingLifecycle;
        std::deque<PendingSnapshot>         m_pendingSnapshots;

        NetId                               m_localNetId        = NetId::Invalid();
        entt::entity                        m_localEntity       = entt::null;

		uint64                              m_lastServerTick    = 0;
        uint32                              m_lastInputAck      = 0;
	};
}
