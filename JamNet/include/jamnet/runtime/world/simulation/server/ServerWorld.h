#pragma once
#include "jamnet/runtime/world/simulation/common/PhysicalWorld.h"
#include "jamnet/runtime/world/simulation/common/ActorComponents.h"
#include "jamnet/runtime/content/world/IWorldContent.h"
#include "jamnet/runtime/world/simulation/server/WorldMetrics.h"

#include <unordered_map>
#include <array>
#include <span>
#include <vector>

namespace jam::net
{
	struct PacketHeaderView;
	class ServerPhysicsSystem;

	enum class ePlayerSpawnFailure : uint8
	{
		None = 0,
		InvalidCorrelation,
		AlreadySpawned,
		SpawnFailed,
	};

	class ServerWorld : public PhysicalWorld
	{
	public:
		using EnterWorldHandler = std::function<void(UserId, const EnterWorldRequest&)>;

		ServerWorld(const WorldConfig& config, std::unique_ptr<IWorldContent> content = {}, EnterWorldHandler enterWorld = {});
		~ServerWorld() override;

		void								Start(uint64 dt_ns) override;
		void								Resume(uint64 dt_ns) override;
		void								Stop() override;

		void								HandleWorldPacket(UserId userId, Packet packet) override;

		bool								AddMember(WorldUserContext user) override;

		void								SendTo(Packet packet, UserId userId) override;
		void								Multicast(Packet packet) override;

		ActorId								SpawnActor(SpawnParams params);
		void								SpawnActorAsync(SpawnParams params, std::function<void(ActorId)> onDone);
		bool								DespawnActor(ActorId actorId, UserId requester = kInvalidUserId);
		void								DespawnActorAsync(ActorId actorId, UserId requester, std::function<void(bool)> onDone);
		void								SpawnPlayerAsync(UserId userId, const WorldEventCorrelation& correlation, SpawnParams params, std::function<void(ActorId, ePlayerSpawnFailure)> onDone);
		bool								DespawnPlayer(UserId userId, const WorldEventCorrelation& correlation, ActorId actorId);
		bool								RestorePlayerControl(UserId userId, ActorId actorId);
		void								PrepareMemberContent(const ServerWorldMemberContentContext& context, IWorldContent::PrepareMemberCompletion completion);
		void								RollbackMemberContent(UserId userId, WorldTransitionToken transitionToken);
		bool								CommitMemberLeave(UserId userId, WorldTransitionToken transitionToken);
		bool								RestoreMemberContent(const WorldUserContext& user, WorldTransitionToken transitionToken);
		void								SuspendMemberReplication(UserId userId);
		bool								ResumeMemberReplication(UserId userId);

		entt::entity						GetControlledEntity(UserId userId) const;
		bool								RequestEnterWorld(UserId userId, EnterWorldRequest request);

	private:
		friend class ServerPhysicsSystem;

		bool								OnInitialize() override;
		void								OnShutdown() override;
		void								Tick() override;
		void								DispatchPhysicsEvents(std::span<const px::PhysicsEvent> events);

		void								OnUserLeft(UserId userId) override;
		void								BootstrapLevelActors();

		void								EnsureUserAoiRegistration(UserId userId);
		void								ApplyInitialControl(entt::entity entity, UserId userId);

		void								ProcessGameInput(UserId userId, const PacketHeaderView& pkt);
		void								FinalizePendingPlayerSpawns();
		void								CompletePendingPlayerSpawn(UserId userId, ActorId actorId, ePlayerSpawnFailure failure);
		void                                SubmitWorldMetrics(uint64 nowNs);

	private:
		std::unordered_map<uint64, entt::entity>	m_userToControlledEntity;

		struct PlayerSpawnResult
		{
			ClientRequestId		clientRequestId = kInvalidClientRequestId;
			ActorId				actorId = ActorId::Invalid();
		};
		std::unordered_map<uint64, PlayerSpawnResult> m_playerSpawnResults;

		struct PendingPlayerSpawn
		{
			WorldEventCorrelation	correlation{};
			ClientRequestId			clientRequestId = kInvalidClientRequestId;
			ActorId					actorId = ActorId::Invalid();
			entt::entity			entity = entt::null;
			uint64					deadlineNs = 0;
			std::vector<std::function<void(ActorId, ePlayerSpawnFailure)>> completions;
		};

		std::unordered_map<uint64, PendingPlayerSpawn> m_pendingPlayerSpawns;
		std::unique_ptr<IWorldContent>			m_content;
		EnterWorldHandler						m_enterWorld;
		bool									m_contentInitialized = false;
		WorldMetrics                            m_metrics;
	};
}
