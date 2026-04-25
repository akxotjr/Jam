#pragma once
#include "jamnet/sync/replication/NetActorComponents.h"
#include "jamnet/sync/replication/ReplicationCodec.h"

#include "jamnet/sync/schema/gen/lifecycle_generated.h"
#include "jamnet/sync/schema/gen/snapshot_generated.h"

namespace jam::net
{
	class ServerAoiSystem;
	class ServerNetWorld;
	class ServerInputSystem;
	class ServerPhysicsSystem;

	struct RigidBaselineState
	{
		px::Vec3   pos = px::Vec3::Zero();
		px::Quat   rot = px::Quat::Identity();
	};

	struct CharBaselineState
	{
		px::Vec3   pos   = px::Vec3::Zero();
		float      yaw   = 0.0f;
		float      pitch = 0.0f;
	};

    struct PendingLifecycleEvent
    {
        fb::fbLifecycleOp    op = fb::fbLifecycleOp_Create;
        fb::fbRemovalReason  reason = fb::fbRemovalReason_AoiLeft;
    };

    struct KnownUserSlot
    {
        uint64  userId = 0;
        bool    alive  = false;
    };

	struct ReplicationUserState
	{
		std::unordered_set<NetId>                              knownActors;
		std::unordered_map<NetId, uint32>                      knownFullEpoch;
		std::unordered_map<NetId, PendingLifecycleEvent>       pendingLifecycle;
		std::unordered_map<NetId, uint8>                       forceFullStateBudget;
	};

	struct SharedRigidState
	{
		uint32                  fullEpoch            = 0;
		uint32                  fullBuiltTick        = 0;
		bool                    hasBaseline          = false;
		bool                    hasPackedDelta       = false;
		RigidBaselineState      baseline             = {};
		px::RigidState          lastDeltaSourceState = {};
		PackedRigidDelta128     packedDelta          = {};
	};

	struct SharedCharState
	{
		uint32                  fullEpoch            = 0;
		uint32                  fullBuiltTick        = 0;
		bool                    hasBaseline          = false;
		bool                    hasPackedDelta       = false;
		CharBaselineState       baseline             = {};
		px::CharacterState      lastDeltaSourceState = {};
		PackedCharDelta128      packedDelta          = {};
	};


	class ServerReplicationSystem
	{
	public:
		ServerReplicationSystem(entt::registry& world);

		void                                        Init();
		void                                        Tick();

		void                                        CaptureSnapshot();
		void                                        ForceLifecycleSyncForUser(uint64 userId, int32 budget = 5);
		void                                        MarkActorDirty(entt::entity e, bool forceMeta = false);

		void                                        OnUserEnter(uint64 userId);
		void                                        OnUserLeave(uint64 userId);
		void                                        OnActorDestroyed(entt::entity e);

	private:
		struct Candidate
		{
			entt::entity    e = entt::null;
			NetId           netId = NetId::Invalid();
			bool            useFull = false;
		};

		enum class eBucket : uint8
		{
			B0_MustSendFull,
			B1_HighDelta,
			B2_NormalDelta,
			B3_LowPriority,
			Count
		};

        struct ActorFrameCache
        {
            entt::entity    e                 = entt::null;
            NetId           netId             = NetId::Invalid();
            uint32          fullEpoch         = 0;
            px::eBodyType   bodyType          = px::eBodyType::None;
            bool            canReplicate      = false;
            bool            isStatic          = false;
            bool            isActive          = false;
            bool            isKinematic       = false;
			size_t          fullSizeEstimate  = 0;
			size_t          deltaSizeEstimate = 0;
		};


		flatbuffers::Offset<fb::fbActorMeta>            BuildActorMeta(entt::entity e, uint64 userId);
		flatbuffers::Offset<fb::fbLifecycleActor>       BuildLifecycleActor(const PendingLifecycleEvent& event, entt::entity e, NetId netId, uint64 userId);
		flatbuffers::Offset<fb::fbActorEntity>          BuildFullActorEntity(entt::entity e, uint64 userId);
		flatbuffers::Offset<fb::fbActorEntity>          BuildDeltaActorEntity(entt::entity e, uint64 userId);

		const ReplicationUserState*                     FindUserState(uint64 userId) const;
		ReplicationUserState*                           FindUserState(uint64 userId);

        uint32                                          GetActorFullEpoch(entt::entity e, NetId netId);
        void                                            RefreshActorFrameCache();
        const ActorFrameCache*                          FindActorFrameCache(NetId netId) const;
        void                                            MarkActorFrameDirty(entt::entity e);
        void                                            UpsertActorFrameCache(entt::entity e, bool isActiveOverride);
        void                                            AddKnownUserToActor(NetId netId, uint64 userId);
        void                                            RemoveKnownUserFromActor(NetId netId, uint64 userId);
        void                                            CompactKnownUsersIfNeeded(NetId netId);

        void                                            QueueLifecycleCreateForUser(uint64 userId, NetId netId);
		void                                            QueueLifecycleMetaForUser(uint64 userId, NetId netId);
        void                                            QueueLifecycleMetaForKnownUser(uint64 userId, NetId netId);
		void                                            QueueRemovalForUser(uint64 userId, NetId netId, fb::fbRemovalReason reason);
		void                                            CancelRemovalForUser(uint64 userId, NetId netId);
		void                                            QueueLifecycleForVisibleActors(uint64 userId, bool forceSyncUser);
		void                                            EmitPendingLifecyclePackets(uint64 userId, uint32 tick);
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
		entt::registry&                                                             m_world;
		std::unique_ptr<flatbuffers::FlatBufferBuilder>                             m_fbb;
		ServerNetWorld*                                                             m_netWorld      = nullptr;
		ServerInputSystem*                                                          m_inputSys      = nullptr;
		ServerAoiSystem*                                                            m_aoiSys        = nullptr;
		ServerPhysicsSystem*                                                        m_physSys       = nullptr;

        std::unordered_map<uint64, ReplicationUserState>                            m_userStates;                 // Per-user replication session state.
        std::unordered_map<NetId, std::vector<KnownUserSlot>>                       m_knownUsersByActor;          // Reverse index from actor to users that know it.
        std::unordered_map<uint64, int32>                                           m_forceLifecycleSyncPerUsers; // Pending forced meta/full sync budget per user.
        std::unordered_map<NetId, SharedRigidState>                                 m_sharedRigidStates;          // Shared rigid full-baseline/delta cache per actor.
        std::unordered_map<NetId, SharedCharState>                                  m_sharedCharacterStates;      // Shared character full-baseline/delta cache per actor.
        std::unordered_map<NetId, ActorFrameCache>                                  m_actorFrameCache;            // Incremental per-actor frame metadata cache.
        std::unordered_map<entt::entity, NetId>                                     m_actorFrameNetIds;           // Reverse lookup for frame cache cleanup.

        std::vector<uint64>                                                         m_usersScratch;               // Reused member list for snapshot iteration.
        std::vector<uint64>                                                         m_knownUsersScratch;          // Reused alive known-user list for actor fan-out.
        std::unordered_set<NetId>                                                   m_sentThisTickScratch;        // Dedup of actors already emitted for one user this tick.
        std::unordered_set<uint32>                                                  m_enteredScratch;             // AOI entered set for quick full-send checks.
        std::array<std::vector<Candidate>, static_cast<size_t>(eBucket::Count)>     m_candidateBucketsScratch;    // Reused candidate buckets by priority.
        std::vector<Candidate>                                                      m_orderedCandidatesScratch;   // Reused flattened candidate list.
        std::vector<flatbuffers::Offset<fb::fbActorEntity>>                         m_actorOffsScratch;           // Reused FlatBuffer actor offsets for one packet.
        std::vector<entt::entity>                                                   m_dirtyActorFrameScratch;     // Dirty actor list for incremental frame-cache updates.
        std::unordered_set<entt::entity>                                            m_dirtyActorFrameDedup;       // Dedup set backing dirty actor list.
        std::unordered_set<entt::entity>                                            m_prevActiveActors;           // Active actors from previous physics tick.
        std::unordered_set<entt::entity>                                            m_currentActiveActorsScratch; // Active actors from current physics tick.

		uint32                                                                      m_fullCacheTick = 0;
		std::unordered_map<NetId, PackedRigidFull192>                               m_cachedRigidFull;
		std::unordered_map<NetId, PackedCharacterFull160>                           m_cachedCharacterFull;

		uint32                                                                      m_tickCounter = 0;


		static constexpr uint32                                                     kTargetTickRate = 33;
		static constexpr uint32                                                     kFullIntervalSec = 5;
		static constexpr uint32                                                     kFullIntervalTicks = kTargetTickRate * kFullIntervalSec;
	};
}
