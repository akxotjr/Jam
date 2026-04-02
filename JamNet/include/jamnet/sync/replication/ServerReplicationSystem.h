#pragma once
#include "jamnet/sync/replication/NetActorComponents.h"
#include "jamnet/sync/replication/ReplicationUtils.h"

#include "jamnet/sync/schema/gen/snapshot_generated.h"

namespace jam::net
{
    class ServerReplicationSystem
    {
    public:
        ServerReplicationSystem(entt::registry& world);

        void                                        Init();
        void                                        Tick();

        void                                        CaptureSnapshot();
        void                                        ForceFullMetaForUser(uint64 userId, int32 budget = 5);

        void                                        OnEnter(uint64 userId);
        void                                        OnLeave(uint64 userId);

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

		struct MetaSentKey
		{
            uint64 userId = 0;
            uint32 netId  = 0;

            friend bool operator==(const MetaSentKey& a, const MetaSentKey& b) noexcept
            {
                return a.userId == b.userId && a.netId == b.netId;
            }
		};

        struct MetaSentKeyHash
        {
	        size_t operator()(const MetaSentKey& k) const noexcept
	        {
                const size_t h1 = std::hash<uint64>{}(k.userId);
                const size_t h2 = std::hash<uint64>{}(k.netId);
                return h1 ^ (h2 + 0x9e3779b97f4a7c15ull + (h1 << 6) + (h1 >> 2));
	        }
        };

        flatbuffers::Offset<fb::fbActorMeta>            BuildActorMeta(entt::entity e, uint64 userId);

        flatbuffers::Offset<fb::fbActorEntity>          BuildFullActorEntity(entt::entity e, uint64 userId, bool includeMeta);
        flatbuffers::Offset<fb::fbActorEntity>          BuildDeltaActorEntity(entt::entity e, uint64 userId);

        void                                            EnsureMetaResendBudget(entt::entity e, const MetaSentKey& key);
    	bool                                            ShouldIncludeMeta(entt::entity e, const MetaSentKey& key);
        void                                            OnMetaSent(entt::entity e, const MetaSentKey& key);
		bool                                            CanClearNewlyCreatedTag(NetId netId);


    private:
        entt::registry&                                     m_world;
        std::unique_ptr<flatbuffers::FlatBufferBuilder>     m_fbb;

        // Snapshot state
        std::unordered_map<uint64, std::unordered_map<NetId, uint32>>                     m_entityBaselinePerUser;           // user -> (netId -> baselineRev)
        std::unordered_map<uint64, std::unordered_map<NetId, RigidBaselineState>>         m_rigidBaselineStatesPerUser;      // user -> (netId -> baseline)
        std::unordered_map<uint64, std::unordered_map<NetId, CharacterBaselineState>>     m_characterBaselineStatesPerUser;  // user -> (netId -> baseline)
        std::unordered_map<uint64, std::unordered_map<NetId, px::KinematicState>>         m_kineBaselineStatesPerUser;

        std::unordered_map<uint64, std::unordered_map<NetId, PackedRigidDelta128>>     m_cachedRigidDeltaPerUser;             // user -> (netId -> packed cache)
        std::unordered_map<uint64, std::unordered_map<NetId, PackedCharacterDelta128>> m_cachedCharacterDeltaPerUser;         // user -> (netId -> packed cache)

        std::unordered_set<MetaSentKey, MetaSentKeyHash>         m_metaSent;
        std::unordered_map<MetaSentKey, uint8, MetaSentKeyHash>  m_metaResendBudget;

        std::unordered_map<uint64, int32>                        m_forceFullMetaPerUsers;

        uint32                                                   m_fullCacheTick = 0;
        std::unordered_map<NetId, PackedRigidFull192>            m_cachedRigidFull;
        std::unordered_map<NetId, PackedCharacterFull160>        m_cachedCharacterFull;

        // --- Snapshot cadence ---
        uint32                                              m_tickCounter = 0;
        static constexpr uint32                             kTargetTickRate = 33;     // 최소 목표
        static constexpr uint32                             kFullIntervalSec = 5;     // 3 또는 5로 조정
        static constexpr uint32                             kFullIntervalTicks = kTargetTickRate * kFullIntervalSec;
    };
}
