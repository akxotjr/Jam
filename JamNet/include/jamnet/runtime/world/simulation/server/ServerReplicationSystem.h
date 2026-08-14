#pragma once
#include "jamnet/runtime/protocol/codec/ReplicationCodec.h"
#include "jamnet/runtime/world/actor/ActorId.h"

#include "jamnet/runtime/protocol/schema/gen/lifecycle_generated.h"
#include "jamnet/runtime/protocol/schema/gen/snapshot_generated.h"
#include "jamnet/runtime/protocol/schema/gen/baseline_ack_generated.h"

namespace jam::net
{
	class ServerAoiSystem;
	class ServerWorld;
	class ServerInputSystem;
	class ServerPhysicsSystem;
	class WorldMetrics;

	struct RigidBaselineState
	{
		px::Vec3   pos = px::Vec3::Zero();
		px::Quat   rot = px::Quat::Identity();
	};

	struct CharBaselineState
	{
		px::Vec3   pos      = px::Vec3::Zero();
		float      bodyYaw  = 0.0f;
		float      viewYaw  = 0.0f;
		float      pitch    = 0.0f;
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

	enum class eReplicationPhase : uint8
	{
		AwaitingPlayer = 0,
		InitialSync,
		Streaming,
		NeedsResync,
		Suspended,
	};

	struct ReplicationUserState
	{
		eReplicationPhase phase = eReplicationPhase::AwaitingPlayer;
		struct BaselineDeliveryState
		{
			uint32 sentBaselineRev = 0;
			uint32 ackedBaselineRev = 0;
			uint32 lastSentTick = 0;
			uint8 resendCount = 0;
		};

		std::unordered_set<ActorId>                          knownActors;
		std::unordered_map<ActorId, BaselineDeliveryState>   baselineDelivery;
		std::unordered_map<ActorId, PendingLifecycleEvent>   pendingLifecycle;
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
		PackedRigidFull192      packedFull           = {};
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
		PackedCharacterFull160  packedFull           = {};
	};


	class ServerReplicationSystem
	{
	public:
		ServerReplicationSystem(entt::registry& world, WorldMetrics& metrics);

		void                                        Init();
		void                                        Tick();

		void                                        CaptureSnapshot();
		void                                        MarkActorDirty(entt::entity e, bool forceMeta = false);

		bool                                        AttachUser(uint64 userId);
		bool                                        BeginInitialSync(uint64 userId);
		bool                                        SuspendUser(uint64 userId);
		bool                                        ResumeUserWithFullSync(uint64 userId);
		bool                                        IsAwaitingPlayer(uint64 userId) const;
		void                                        HandleBaselineFeedback(uint64 userId, const fb::fbBaselineAckBatch& batch);
		void                                        OnUserLeave(uint64 userId);
		void                                        OnActorDestroyed(entt::entity e);

	private:
		struct Candidate
		{
			entt::entity    e = entt::null;
			ActorId         actorId = ActorId::Invalid();
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
            ActorId         actorId           = ActorId::Invalid();
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
		flatbuffers::Offset<fb::fbLifecycleActor>       BuildLifecycleActor(const PendingLifecycleEvent& event, entt::entity e, ActorId actorId, uint64 userId);
		flatbuffers::Offset<fb::fbActorEntity>          BuildFullActorEntity(entt::entity e, uint64 userId);
		flatbuffers::Offset<fb::fbActorEntity>          BuildDeltaActorEntity(entt::entity e, uint64 userId);

		const ReplicationUserState*                     FindUserState(uint64 userId) const;
		ReplicationUserState*                           FindUserState(uint64 userId);

        uint32                                          GetActorFullEpoch(entt::entity e, ActorId actorId);
        void                                            RefreshActorFrameCache();
        const ActorFrameCache*                          FindActorFrameCache(ActorId actorId) const;
        void                                            MarkActorFrameDirty(entt::entity e);
        void                                            UpsertActorFrameCache(entt::entity e, bool isActiveOverride);
        void                                            AddKnownUserToActor(ActorId actorId, uint64 userId);
        void                                            RemoveKnownUserFromActor(ActorId actorId, uint64 userId);
        void                                            CompactKnownUsersIfNeeded(ActorId actorId);

        void                                            QueueLifecycleCreateForUser(uint64 userId, ActorId actorId);
		void                                            QueueLifecycleMetaForUser(uint64 userId, ActorId actorId);
		void                                            QueueLifecycleMetaForKnownUser(uint64 userId, ActorId actorId);
		void                                            QueueRemovalForUser(uint64 userId, ActorId actorId, fb::fbRemovalReason reason);
		void                                            CancelRemovalForUser(uint64 userId, ActorId actorId);
		void                                            QueueLifecycleForVisibleActors(uint64 userId);
		void                                            EmitPendingLifecyclePackets(uint64 userId, uint32 tick);
		void                                            CommitPendingLifecycleBatch(uint64 userId, const std::vector<std::pair<ActorId, PendingLifecycleEvent>>& sentEvents);

		void                                            InvalidateUserCaches(uint64 userId, ActorId actorId);
		void                                            InvalidateAllUserCaches(ActorId actorId);

		bool                                            IsActorKnownToUser(uint64 userId, ActorId actorId) const;
		void                                            MarkActorKnownToUser(uint64 userId, ActorId actorId);
		void                                            ForgetActorForUser(uint64 userId, ActorId actorId);
		void                                            ForgetActorForAllUsers(ActorId actorId);
		bool                                            CanSendDelta(uint64 userId, ActorId actorId, uint32 fullEpoch) const;
		bool                                            ShouldSendFull(uint64 userId, ActorId actorId, uint32 fullEpoch);
		void                                            TryCompleteInitialSync(uint64 userId);
		void                                            MarkFullStateSent(uint64 userId, ActorId actorId);

	private:
		entt::registry&                                                             m_world;
		std::unique_ptr<flatbuffers::FlatBufferBuilder>                             m_fbb;
		ServerWorld*                                                        m_netWorld      = nullptr;
		ServerInputSystem*                                                          m_inputSys      = nullptr;
		ServerAoiSystem*                                                            m_aoiSys        = nullptr;
		ServerPhysicsSystem*                                                        m_physSys       = nullptr;
		WorldMetrics*																m_metrics       = nullptr;

        std::unordered_map<uint64, ReplicationUserState>                            m_userStates;                 // Per-user replication session state.
        std::unordered_map<ActorId, std::vector<KnownUserSlot>>                       m_knownUsersByActor;          // Reverse index from actor to users that know it.
        std::unordered_map<ActorId, SharedRigidState>                                 m_sharedRigidStates;          // Shared rigid full-baseline/delta cache per actor.
        std::unordered_map<ActorId, SharedCharState>                                  m_sharedCharacterStates;      // Shared character full-baseline/delta cache per actor.
        std::unordered_map<ActorId, ActorFrameCache>                                  m_actorFrameCache;            // Incremental per-actor frame metadata cache.
        std::unordered_map<entt::entity, ActorId>                                     m_actorFrameActorIds;           // Reverse lookup for frame cache cleanup.

		std::vector<uint64>                                                         m_knownUsersScratch;          // Reused alive known-user list for actor fan-out.
        std::unordered_set<ActorId>                                                   m_sentThisTickScratch;        // Dedup of actors already emitted for one user this tick.
        std::unordered_set<uint32>                                                  m_enteredScratch;             // AOI entered set for quick full-send checks.
        std::array<std::vector<Candidate>, static_cast<size_t>(eBucket::Count)>     m_candidateBucketsScratch;    // Reused candidate buckets by priority.
        std::vector<Candidate>                                                      m_orderedCandidatesScratch;   // Reused flattened candidate list.
        std::vector<flatbuffers::Offset<fb::fbActorEntity>>                         m_actorOffsScratch;           // Reused FlatBuffer actor offsets for one packet.
		std::vector<ActorId>															m_fullActorIdsScratch;
        std::vector<entt::entity>                                                   m_dirtyActorFrameScratch;     // Dirty actor list for incremental frame-cache updates.
        std::unordered_set<entt::entity>                                            m_dirtyActorFrameDedup;       // Dedup set backing dirty actor list.
        std::unordered_set<entt::entity>                                            m_prevActiveActors;           // Active actors from previous physics tick.
        std::unordered_set<entt::entity>                                            m_currentActiveActorsScratch; // Active actors from current physics tick.

		uint32                                                                      m_tickCounter = 0;

		static constexpr uint32                                                     kBaselineResendTicks = 10;
		static constexpr uint8                                                      kBaselineResendBudget = 8;
	};
}
