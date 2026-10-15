#pragma once
#include <entt/entity/sparse_set.hpp>
#include <span>
#include "jamnet/runtime/world/simulation/server/ServerAoiTypes.h"
#include "jamnet/runtime/world/simulation/server/ActorLifecycleTypes.h"


#include "jamnet/runtime/protocol/codec/ReplicationCodec.h"
#include "jamnet/runtime/world/actor/ActorId.h"

#include "jamnet/runtime/protocol/schema/gen/lifecycle_generated.h"
#include "jamnet/runtime/protocol/schema/gen/snapshot_generated.h"
#include "jamnet/runtime/protocol/schema/gen/baseline_ack_generated.h"

namespace jam::net
{
	struct ReplicationUserInput
	{
		uint64	userId					= 0;
		uint32	appliedInputSequence	= 0;
		uint32	appliedControlRevision	= 0;

		const UserAoiState*						aoiState	  = nullptr;
	};

	class ServerWorld;
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

    struct KnownUserSlot
    {
        uint64  userId = 0;
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
		using PendingLifecycleMap = std::unordered_map<ActorId, eActorLifecycleOp>;

		struct BaselineDeliveryState
		{
			uint32 sentBaselineRev	= 0;
			uint32 ackedBaselineRev = 0;
			uint32 lastSentTick		= 0;
			uint8  resendCount		= 0;
		};

		eReplicationPhase									 phase = eReplicationPhase::Suspended;
		std::unordered_map<ActorId, size_t>                  knownActors; // Introduced Meta -> reverse user-slot index; retained outside AOI.
		std::unordered_set<ActorId>                          activeActors; // Committed AOI activation.
		std::unordered_map<ActorId, BaselineDeliveryState>   baselineDelivery;
		PendingLifecycleMap									 pendingLifecycle;
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


	// Lifecycle contract:
	// - Created is the first introduction of an AOI-visible actor to a user,
	//   regardless of the actor's server creation tick. It carries immutable Meta.
	//   owned/controlled describe the receiving user; controlled implies owned.
	// - AoiEntered reactivates an already introduced actor using cached Meta.
	// - AoiLeft removes the client actor but preserves Meta and server knowledge.
	// - Destroyed removes both the client actor and Meta, including outside AOI;
	//   notify every user previously introduced to the actor.
	// - Only Created carries Meta. Its cache lifetime ends at Destroyed, or at
	//   the corresponding World/session reset. AOI visibility is a separate state.
	// - Lifecycle events are submitted once logically over RELIABLE_ORDERED.
	//   Snapshot Full/Delta and baseline ACK remain separate; re-entry needs Full.
	class ServerReplicationSystem
	{
	public:
		ServerReplicationSystem(ServerWorld& world, entt::registry& registry, WorldMetrics& metrics);

		void                                        Init();
		// World calls BeginTick once, ReplicateUser once per user, then EndTick.
		// Runs without yield/reentry on the owning shard; user input is not retained.
		void                                        BeginTick(std::span<const entt::entity> activeEntities);
		void                                        ReplicateUser(const ReplicationUserInput& user, uint32 currentTick);
		void                                        EndTick();

		void                                        MarkActorDirty(entt::entity e);

		// Registration starts suspended; activation explicitly resumes replication.
		bool                                        AttachUser(uint64 userId);
		bool                                        BeginInitialSync(uint64 userId);
		bool                                        SuspendUser(uint64 userId);
		bool                                        ResumeUserWithFullSync(uint64 userId, bool playerReady);
		bool                                        IsAwaitingPlayer(uint64 userId) const;
		bool IsSuspended(uint64 userId) const;
		void                                        AwaitPlayer(uint64 userId);
		void                                        HandleBaselineFeedback(uint64 userId, const fb::fbBaselineAckBatch& batch, const UserAoiState* aoiState);
		void                                        OnUserLeave(uint64 userId);
		void                                        OnActorDestroyed(entt::entity e);

	private:
		struct Candidate
		{
			entt::entity    e = entt::null;
			ActorId         actorId = ActorId::Invalid();
			bool            useFull = false;
			size_t          sizeEstimate = 0;
		};

        // Contains only actors eligible for Snapshot serialization.
        struct ActorFrameCache
        {
            entt::entity    e                 = entt::null;
            ActorId         actorId           = ActorId::Invalid();
            uint32          fullEpoch         = 0;
            px::eBodyType   bodyType          = px::eBodyType::None;
            bool            isActive          = false;
            bool            isKinematic       = false;
			// Covers both Full and Delta, including Delta-to-Full fallback.
			size_t          sizeEstimate      = 0;
		};


		flatbuffers::Offset<fb::fbActorMeta>            BuildActorMeta(uint64 userId, entt::entity e);
		flatbuffers::Offset<fb::fbLifecycleActor>       BuildLifecycleActor(uint64 userId, eActorLifecycleOp op, ActorId actorId);
		flatbuffers::Offset<fb::fbActorEntity>          BuildFullActorEntity(uint64 userId, entt::entity e, uint32 currentTick);
		flatbuffers::Offset<fb::fbActorEntity>          BuildDeltaActorEntity(uint64 userId, entt::entity e, OUT bool& builtFull, uint32 currentTick);

		const ReplicationUserState*                     FindUserState(uint64 userId) const;
		ReplicationUserState*                           FindUserState(uint64 userId);

        uint32                                          GetActorFullEpoch(ActorId actorId, entt::entity e);
        void                                            RefreshActorFrameCache(std::span<const entt::entity> activeEntities);
        const ActorFrameCache*                          FindActorFrameCache(entt::entity e) const;
        void                                            MarkActorFrameDirty(entt::entity e);
        void                                            UpsertActorFrameCache(entt::entity e, bool isActiveOverride);
        void                                            AddKnownUserToActor(uint64 userId, ActorId actorId);
        void                                            RemoveKnownUserFromActor(uint64 userId, ActorId actorId);

        void                                            QueueLifecycleEnterForUser(uint64 userId, ActorId actorId);
		void                                            QueueRemovalForUser(uint64 userId, ActorId actorId, eActorLifecycleOp op);
		void                                            QueueLifecycleForVisibleActors(uint64 userId, const UserAoiState* state);
		void                                            EmitPendingLifecyclePackets(uint64 userId, uint32 tick);
		void                                            CommitPendingLifecycleBatch(uint64 userId, std::span<const ReplicationUserState::PendingLifecycleMap::iterator> sentEvents);

		void                                            InvalidateUserCaches(uint64 userId, ActorId actorId);
		void                                            InvalidateAllUserCaches(ActorId actorId);

		void                                            MarkActorKnownToUser(uint64 userId, ActorId actorId);
		void                                            ForgetActorForUser(uint64 userId, ActorId actorId);
		bool                                            CanSendDelta(uint64 userId, ActorId actorId, uint32 fullEpoch) const;
		bool                                            CanSendDelta(const ReplicationUserState& userState, entt::entity entity, const ReplicationUserState::BaselineDeliveryState* delivery, uint32 fullEpoch) const;
		bool                                            ShouldSendFull(ReplicationUserState::BaselineDeliveryState& delivery, uint32 fullEpoch, uint32 currentTick);
		void                                            TryCompleteInitialSync(uint64 userId, const UserAoiState* aoiState);
		void                                            MarkFullStateSent(uint64 userId, ActorId actorId, uint32 currentTick);

	private:
		ServerWorld&																m_world;
		entt::registry&                                                             m_registry;
		std::unique_ptr<flatbuffers::FlatBufferBuilder>                             m_fbb;
		WorldMetrics&																m_metrics;

        std::unordered_map<uint64, ReplicationUserState>                            m_userStates;                 // Per-user replication session state.
        std::unordered_map<ActorId, std::vector<KnownUserSlot>>                       m_knownUsersByActor;          // Reverse index from actor to users that know it.
        std::unordered_map<ActorId, SharedRigidState>                                 m_sharedRigidStates;          // Shared rigid full-baseline/delta cache per actor.
        std::unordered_map<ActorId, SharedCharState>                                  m_sharedCharacterStates;      // Shared character full-baseline/delta cache per actor.
        std::unordered_map<entt::entity, ActorFrameCache>                             m_actorFrameCache;            // Incremental per-entity frame metadata cache.

		std::vector<uint64>                                                         m_knownUsersScratch;          // Reused alive known-user list for actor fan-out.
		std::vector<Candidate>                                                      m_snapshotCandidatesScratch;
        std::vector<flatbuffers::Offset<fb::fbActorEntity>>                         m_actorOffsScratch;           // Reused FlatBuffer actor offsets for one packet.
		std::vector<ActorId>														m_fullActorIdsScratch;
		std::vector<flatbuffers::Offset<fb::fbLifecycleActor>>                       m_lifecycleActorOffsScratch;
		std::vector<ReplicationUserState::PendingLifecycleMap::iterator>           m_lifecycleBatchScratch;
        std::vector<entt::entity>                                                   m_dirtyActorFrameScratch;     // Dirty actor list for incremental frame-cache updates.
        std::unordered_set<entt::entity>                                            m_dirtyActorFrameDedup;       // Dedup set backing dirty actor list.
        std::unordered_set<entt::entity>                                            m_prevActiveActors;           // Active actors from previous physics tick.
        std::unordered_set<entt::entity>                                            m_currentActiveActorsScratch; // Active actors from current physics tick.



		static constexpr uint32                                                     kBaselineResendTicks = 10;
		static constexpr uint8                                                      kBaselineResendBudget = 8;
	};
}
