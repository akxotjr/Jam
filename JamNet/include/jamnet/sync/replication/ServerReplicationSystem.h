#pragma once
#include "jamnet/sync/replication/NetActorComponents.h"
#include "jamnet/sync/replication/ReplicationUtils.h"

#include "jamnet/sync/schema/gen/lifecycle_generated.h"
#include "jamnet/sync/schema/gen/snapshot_generated.h"

namespace jam::net
{
    class ServerAoiSystem;
    class ServerNetWorld;

    class ServerReplicationSystem
    {
    public:
        ServerReplicationSystem(entt::registry& world);

        void                                        Init();
        void                                        Tick();

        void                                        CaptureSnapshot();
        void                                        ForceLifecycleSyncForUser(uint64 userId, int32 budget = 5);
        void                                        MarkActorDirty(entt::entity e, bool forceMeta = false);

        void                                        OnEnter(uint64 userId);
        void                                        OnLeave(uint64 userId);
        void                                        OnActorDestroyed(entt::entity e);

    private:
        struct RigidBaselineState
        {
            px::Vec3   pos = px::Vec3::Zero();
            px::Quat   rot = px::Quat::Identity();
        };

        struct CharacterBaselineState
        {
            px::Vec3   pos   = px::Vec3::Zero();
            float      yaw   = 0.0f;
            float      pitch = 0.0f;
        };

        struct ActorUserKey
        {
            uint64 userId = 0;
            uint32 netId  = 0;

            friend bool operator==(const ActorUserKey& a, const ActorUserKey& b) noexcept
            {
                return a.userId == b.userId && a.netId == b.netId;
            }
        };

        struct ActorUserKeyHash
        {
            size_t operator()(const ActorUserKey& k) const noexcept
            {
                const size_t h1 = std::hash<uint64>{}(k.userId);
                const size_t h2 = std::hash<uint32>{}(k.netId);
                return h1 ^ (h2 + 0x9e3779b97f4a7c15ull + (h1 << 6) + (h1 >> 2));
            }
        };

        struct PendingLifecycleEvent
        {
            fb::fbLifecycleOp    op     = fb::fbLifecycleOp_Create;
            fb::fbRemovalReason  reason = fb::fbRemovalReason_AoiLeft;
        };

        flatbuffers::Offset<fb::fbActorMeta>            BuildActorMeta(entt::entity e, uint64 userId);
        flatbuffers::Offset<fb::fbLifecycleActor>       BuildLifecycleActor(const PendingLifecycleEvent& event, entt::entity e, NetId netId, uint64 userId);
        flatbuffers::Offset<fb::fbActorEntity>          BuildFullActorEntity(entt::entity e, uint64 userId);
        flatbuffers::Offset<fb::fbActorEntity>          BuildDeltaActorEntity(entt::entity e, uint64 userId);

        void                                            QueueLifecycleCreateForUser(uint64 userId, NetId netId);
        void                                            QueueLifecycleMetaForUser(uint64 userId, NetId netId);
        void                                            QueueRemovalForUser(uint64 userId, NetId netId, fb::fbRemovalReason reason);
        void                                            CancelRemovalForUser(uint64 userId, NetId netId);
        void                                            QueueLifecycleForVisibleActors(uint64 userId, const ServerAoiSystem* aoi, bool forceSyncUser);
        void                                            EmitPendingLifecyclePackets(ServerNetWorld& nw, uint64 userId, uint32 tick);
        void                                            CommitPendingLifecycleBatch(uint64 userId, const std::vector<std::pair<NetId, PendingLifecycleEvent>>& sentEvents);

        void                                            InvalidateUserCaches(uint64 userId, NetId netId);
        void                                            InvalidateAllUserCaches(NetId netId);

        bool                                            IsActorKnownToUser(uint64 userId, NetId netId) const;
        void                                            MarkActorKnownToUser(uint64 userId, NetId netId);
        void                                            ForgetActorForUser(uint64 userId, NetId netId);
        void                                            ForgetActorForAllUsers(NetId netId);
        bool                                            ShouldForceFullState(uint64 userId, NetId netId) const;
        void                                            MarkFullStateSent(uint64 userId, NetId netId);

    private:
        entt::registry&                                     m_world;
        std::unique_ptr<flatbuffers::FlatBufferBuilder>     m_fbb;

        std::unordered_map<uint64, std::unordered_map<NetId, uint32>>                     m_entityBaselinePerUser;
        std::unordered_map<uint64, std::unordered_map<NetId, RigidBaselineState>>         m_rigidBaselineStatesPerUser;
        std::unordered_map<uint64, std::unordered_map<NetId, CharacterBaselineState>>     m_characterBaselineStatesPerUser;
        std::unordered_map<uint64, std::unordered_map<NetId, px::KinematicState>>         m_kineBaselineStatesPerUser;

        std::unordered_map<uint64, std::unordered_map<NetId, PackedRigidDelta128>>        m_cachedRigidDeltaPerUser;
        std::unordered_map<uint64, std::unordered_map<NetId, PackedCharacterDelta128>>    m_cachedCharacterDeltaPerUser;

        std::unordered_set<ActorUserKey, ActorUserKeyHash>                                m_knownActorsPerUser;
        std::unordered_map<uint64, std::unordered_map<NetId, PendingLifecycleEvent>>      m_pendingLifecyclePerUser;
        std::unordered_map<uint64, int32>                                                  m_forceLifecycleSyncPerUsers;
        std::unordered_map<uint64, std::unordered_map<NetId, uint8>>                      m_forceFullStateBudgetPerUser;

        uint32                                                                             m_fullCacheTick = 0;
        std::unordered_map<NetId, PackedRigidFull192>                                      m_cachedRigidFull;
        std::unordered_map<NetId, PackedCharacterFull160>                                  m_cachedCharacterFull;

        uint32                                                                             m_tickCounter = 0;
        static constexpr uint32                                                            kTargetTickRate = 33;
        static constexpr uint32                                                            kFullIntervalSec = 5;
        static constexpr uint32                                                            kFullIntervalTicks = kTargetTickRate * kFullIntervalSec;
    };
}
