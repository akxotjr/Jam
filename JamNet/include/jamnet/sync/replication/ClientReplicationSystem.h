#pragma once
#include "ReplicationTypes.h"
#include "jamnet/sync/schema/gen/snapshot_generated.h"

namespace jam::net
{

    struct Replica
    {
        uint64          netId           = 0;
        entt::entity    e            = entt::null;
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


    /**
     * @class ClientReplicationSystem
     * 
     * @brief Snapshot 처리, 엔티티 동기화
     * @details 
     *  - Snapshot 수신 및 파싱
     *  - 원격 액터: NetTransform 직접 갱신 + 보간 버퍼 저장
     *  - 로컬 액터: ServerState 버퍼링 (PhysicsSystem 에서 Reconcile용으로 사용)
     *  - Ownership, Control 태그 관리
     */
	class ClientReplicationSystem
	{
    public:
        explicit ClientReplicationSystem(entt::registry& world);

        void                                Init();
        void                                Clear();
        void                                Tick();

        void                                EnqueueSnapshot(fb::fbSnapshotT snapshot);

       // optional<ServerState>               ConsumePendingServerState();
        //bool                                HasPendingServerState() const { return m_pendingServerState.has_value(); }

        bool                                IsLocalActor(uint32 netId) const { return netId == m_localNetId; }

        uint64                              GetLastServerTick() const { return m_lastServerTick; }
        uint32                              GetLastInputAck() const { return m_lastInputAck; }

        uint32                              GetLocalNetId() const { return m_localNetId; }
        void                                SetLocalNetId(uint32 netId) { m_localNetId = netId; }
        entt::entity                        GetLocalEntity() const { return m_localEntity; }

    private:
        void                                ProcessEntity(const fb::fbActorEntityT& ent, uint64 serverTick);
        entt::entity                        ResolveEntityForSnapshot(uint32 netId, const fb::fbActorMetaT* meta);
        
		void                                ApplyRigidFullSnapshot(uint64 serverTick, uint32 netId, const fb::fbTransformFull* tf, uint32 baselineRev);
        void                                ApplyRigidDeltaSnapshot(uint64 serverTick, uint32 netId, const fb::fbTransformDelta* tf, uint32 baselineRev);
        
        void                                ApplyCharacterFullSnapshot(uint64 serverTick, uint32 netId, const fb::fbCharacterFull160* ch, uint32 baselineRev, bool isLocal);
        void                                ApplyCharacterDeltaSnapshot(uint64 serverTick, uint32 netId, const fb::fbCharacterDelta128* ch, uint32 baselineRev, bool isLocal);

		Replica&                            GetOrCreateReplica(uint32 netId, bool* created = nullptr);
        void                                PruneOldReplicas(uint64 serverTick, uint64 forgetAfterTicks = 300);

        // local = owner == myUserId && controller == myUserId, and only when meta exists.
        void                                UpdateUniqueLocalFromMeta(uint32 netId, const fb::fbActorMetaT& meta, Replica& replica);

    private:
        entt::registry&                     m_world;

        uint64                              m_userId = 0;

        unordered_map<uint64, Replica>      m_replicas;     // net id -> Replica
        deque<fb::fbSnapshotT>              m_pendingSnapshots;
       // optional<ServerState>               m_pendingServerState;  // 로컬 액터용 서버 상태 (PhysicsSystem 에서 Consume)

        uint32                              m_localNetId = 0;
        entt::entity                        m_localEntity = entt::null;

		uint64                              m_lastServerTick = 0;
        uint32                              m_lastInputAck = 0;
	};

}
