#pragma once
#include "jamnet/runtime/world/simulation/common/PhysicalWorld.h"
#include "jamnet/runtime/world/simulation/common/ActorComponents.h"
#include "jamnet/runtime/world/simulation/common/WorldContext.h"
#include "jamnet/runtime/content/world/IWorldContent.h"
#include "jamnet/runtime/world/simulation/server/WorldMetrics.h"

#include <unordered_map>
#include <array>
#include <span>
#include <vector>
#include <utility>

namespace jam::net
{
	struct PacketHeaderView;
	class ServerActorLifecycleSystem;
	class ServerInputSystem;
	class ServerPhysicsSystem;
	class ServerAoiSystem;
	class ServerReplicationSystem;

	enum class ePlayerSpawnFailure : uint8
	{
		None = 0,
		InvalidCorrelation,
		AlreadySpawned,
		SpawnFailed,
	};


	// Invariant: subsystems are initialized before World operations begin and
	// remain owned by this ServerWorld until its destruction. During operation,
	// they must never become nullptr, be reset/replaced, or be moved out (std::move).
	class ServerWorld : public PhysicalWorld
	{
	public:
		using EnterWorldHandler = std::function<void(UserId, const EnterWorldRequest&)>;

		ServerWorld(const WorldConfig& config, std::unique_ptr<IWorldContent> content = {}, EnterWorldHandler enterWorld = {});
		~ServerWorld() override;

		void								Start(Duration dt) override;
		void								Resume(Duration dt) override;
		void								Stop() override;

		void								HandleWorldPacket(UserId userId, Packet packet) override;

		void								SendTo(Packet packet, UserId userId) override;
		// Returns session job submission success, not packet delivery.
		bool								TrySendTo(Packet packet, UserId userId);
		void								Multicast(Packet packet) override;

		// Caller must queue a job to the owning shard; this method performs the spawn.
		// Completion runs here after state is committed. Queue another job for follow-up lifecycle work.
		// Duplicate requests reuse the result while it remains in the bounded cache.
		void								SpawnActorAsync(UserId requester, SpawnParams params, std::function<void(ActorId)> onDone = {});
		void								DespawnActorAsync(UserId requester, ActorId actorId, std::function<void(bool)> onDone = {});
		// Player is the single Actor a User can control in this World (zero or one per Member).
		// During a transition, an Actor retained for rollback is not a second committed Player.
		// Completion means AOI is ready. Suspended replication waits for explicit resume.
		void								SpawnPlayerAsync(UserId userId, const WorldEventCorrelation& correlation, SpawnParams params, std::function<void(ActorId, ePlayerSpawnFailure)> onDone);
		void								DespawnPlayerAsync(UserId userId, const WorldEventCorrelation& correlation, ActorId actorId, std::function<void(bool)> onDone = {});

		bool								RestorePlayerBinding(UserId userId, ActorId actorId);
		// Membership operations. Content decisions belong to this World; callers
		// provide transport/entry data and wait for the result, not content phases.
		// Invoke on the owning shard outside Tick; WorldShardState accounts for capacity and pins.
		eWorldTransitionFailure JoinMembership(WorldUserContext user, WorldTransitionToken token, std::string entryPoint);
		bool DetachMembership(UserId userId, WorldTransitionToken token);
		bool CancelMembership(UserId userId, WorldTransitionToken token);
		bool RecoverMembership(WorldUserContext user, WorldTransitionToken token);
		bool CompleteMembershipLeave(UserId userId, WorldTransitionToken token);
		bool CompleteMembershipEntry(UserId userId, WorldTransitionToken token);
		// Called after client application. Transport/revision must still match the Member.
		bool ActivateMemberReplication(const WorldUserContext& user);
		bool HasMembershipWork() const
		{
			return !m_membershipEntries.empty() || !m_detachedMemberships.empty();
		}
		bool HasMembershipEntry(WorldTransitionToken token) const { return m_membershipEntries.contains(token); }
		bool HasDetachedMembership(WorldTransitionToken token) const { return m_detachedMemberships.contains(token); }
		void								SuspendMemberReplication(UserId userId);
		bool								ResumeMemberReplication(UserId userId);

		entt::entity						GetPlayerEntity(UserId userId) const;
		bool								RequestEnterWorld(UserId userId, EnterWorldRequest request);

	private:
		friend class ServerActorLifecycleSystem;
		friend class ServerPhysicsSystem;
		friend class ServerAoiSystem;
		friend class ServerReplicationSystem;

		px::eMotionType						FindActorMotionType(px::PhysicsArchetypeKey archetype) const;
		px::eBodyType						FindActorBodyType(px::PhysicsArchetypeKey archetype) const;

		bool								OnInitialize() override;
		void								OnCloseStarted() override;
		void								Tick() override;
		void								DispatchPhysicsEvents(std::span<const px::PhysicsEvent> events);

		bool								OnUserEntered(UserId userId) override;
		void								OnUserLeft(UserId userId) override;
		void								BootstrapLevelActors();


		bool								BindPlayer(UserId userId, entt::entity entity, bool alreadyBound = false);
		void								UnbindPlayer(UserId userId);

		void								ProcessGameInput(UserId userId, const PacketHeaderView& pkt);

		std::pair<ActorId, entt::entity>	ExecuteSpawnActor(SpawnParams params, bool clientRequest);
		bool								ExecuteDespawnActor(UserId requester, ActorId actorId);

		void                                SubmitWorldMetrics(Timestamp now);

	private:
		std::unordered_map<uint64, entt::entity>	m_userToPlayerEntity;
		struct MembershipEntry
		{
			UserId userId = kInvalidUserId;
		};
		std::unordered_map<WorldTransitionToken, MembershipEntry, WorldTransitionTokenHash> m_membershipEntries;
		std::unordered_map<UserId, WorldMembershipContext> m_pendingActivations;
		std::unordered_map<WorldTransitionToken, WorldUserContext, WorldTransitionTokenHash> m_detachedMemberships;

		std::unique_ptr<IWorldContent>					m_content;
		EnterWorldHandler								m_enterWorld;
		bool											m_contentInitialized = false;
		WorldMetrics									m_metrics;

		TickCounter										m_tickCounter;
		std::unique_ptr<ServerActorLifecycleSystem>		m_actorSystem;
		std::unique_ptr<ServerInputSystem>				m_inputSystem;
		std::unique_ptr<ServerPhysicsSystem>			m_physicsSystem;
		std::unique_ptr<ServerAoiSystem>				m_aoiSystem;
		std::unique_ptr<ServerReplicationSystem>		m_replicationSystem;
	};
}
