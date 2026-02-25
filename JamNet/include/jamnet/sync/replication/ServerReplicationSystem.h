#pragma once
#include "jamnet/sync/schema/gen/snapshot_generated.h"
#include "jamnet/sync/replication/ReplicationUtils.h"

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
		bool                                            CanClearNewlyCreatedTag(uint32 netId);


    private:
        entt::registry&                                     m_world;
        unique_ptr<flatbuffers::FlatBufferBuilder>          m_fbb;

        // Snapshot state
        unordered_map<uint64, unordered_map<uint32, uint32>>                 m_entityBaselinePerUser;           // user -> (netId -> baselineRev)
        unordered_map<uint64, unordered_map<uint32, RigidBaselineState>>     m_rigidBaselineStatesPerUser;      // user -> (netId -> baseline)
        unordered_map<uint64, unordered_map<uint32, CharacterBaselineState>> m_characterBaselineStatesPerUser;  // user -> (netId -> baseline)

        unordered_map<uint64, unordered_map<uint32, PackedRigidDelta128>>     m_cachedRigidDeltaPerUser;
        unordered_map<uint64, unordered_map<uint32, PackedCharacterDelta128>> m_cachedCharacterDeltaPerUser;

        unordered_set<MetaSentKey, MetaSentKeyHash>         m_metaSent;
        unordered_map<MetaSentKey, uint8, MetaSentKeyHash>  m_metaResendBudget;

        unordered_map<uint64, int32>                        m_forceFullMetaPerUsers;

        uint32                                              m_fullCacheTick = 0;
        unordered_map<uint32, PackedRigidFull192>           m_cachedRigidFull;
        unordered_map<uint32, PackedCharacterFull160>       m_cachedCharacterFull;

        // --- Snapshot cadence ---
        uint32                                              m_tickCounter = 0;
        static constexpr uint32                             kTargetTickRate = 33;     // 최소 목표
        static constexpr uint32                             kFullIntervalSec = 5;     // 3 또는 5로 조정
        static constexpr uint32                             kFullIntervalTicks = kTargetTickRate * kFullIntervalSec;
    };
}
