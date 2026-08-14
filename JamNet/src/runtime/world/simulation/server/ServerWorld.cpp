#include "pch.h"

#include "jamnet/core/net/PacketBuilder.h"
#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/core/executor/GlobalExecutor.h"
#include "jamnet/core/utils/ScopedTimer.h"

#include "jamnet/runtime/world/simulation/server/ServerWorld.h"
#include "jamnet/core/executor/ShardExecutor.h"
#include "jamnet/runtime/world/simulation/common/ActorComponents.h"
#include "jamnet/runtime/world/simulation/common/WorldContext.h"
#include "jamnet/runtime/world/simulation/common/ShardJobBridge.h"
#include "jamnet/runtime/world/simulation/server/ServerInputSystem.h"
#include "jamnet/runtime/world/simulation/server/ServerPhysicsSystem.h"
#include "jamnet/runtime/world/simulation/server/ServerAoiSystem.h"
#include "jamnet/runtime/world/simulation/server/ServerReplicationSystem.h"

#include "jamnet/runtime/session/ServerSession.h"

#include "jamnet/runtime/protocol/transport/CustomPacketHelper.h"
#include "jamnet/runtime/protocol/schema/gen/input_generated.h"

#include <jampx/PhysicsFacade.h>

namespace jam::net
{

	namespace
	{
		Packet ClonePacket(const Packet& packet)
		{
			if (!packet.IsValid())
				return {};

			const uint32 size = packet->Size();
			BufWriter writer(GetNetBufferPool(eNetBufferPoolKind::Clone));
			BufferSlice slice = writer.OpenForPayload(size, alignof(PacketHeader));
			WritePayload(slice, packet->Head(), size);
			slice.Close();
			return MakeOwned(slice);
		}

		eProtocolType ResolveProtocol(const Packet& packet)
		{
			if (!packet.IsValid())
				return eProtocolType::NONE;

			const PacketHeaderView view = PacketHeaderView::Parse(packet->Head(), packet->Size());
			if (!view.IsValid())
				return eProtocolType::NONE;

			return IsTcp(view.Channel()) ? eProtocolType::TCP : eProtocolType::UDP;
		}

		template<typename SessionT>
		bool PostToSession(const SessionRef<SessionT>& sessionRef, Packet packet)
		{
			if (!packet.IsValid())
				return false;

			const SessionRef<SessionT> target = sessionRef;
			const bool posted = target.TryPost(Job([target, packet = std::move(packet)]() mutable
				{
					SessionT* session = target.TryGet();
					if (!session || session->IsClosing() || !session->IsConnected())
						return;

					session->Send(std::move(packet));
				}));

			return posted;
		}

		bool PostToSession(const ServerSessionBundle& sessions, eProtocolType protocol, Packet packet)
		{
			if (protocol == eProtocolType::TCP)
				return PostToSession(sessions.tcp, std::move(packet));
			if (protocol == eProtocolType::UDP)
				return PostToSession(sessions.udp, std::move(packet));

			return false;
		}

		px::ActorId BindingTarget(entt::registry& world, const entt::entity e, ActorId target, const ActorDirectory& actors)
		{
			if (!target.IsValid()) return px::INVALID_ACTOR_ID;

			const entt::entity candidate = actors.Resolve(target);
			if (candidate != entt::null && world.valid(candidate) && world.all_of<PhysicsSpawnedTag>(candidate))
			{
				const px::ActorId resolved = GetPhysicsActorId(world, candidate);
				world.emplace<TargetInfo>(e, TargetInfo{ .targetActorId = target, .resolvedActorId = resolved });
				return resolved;
			}

			return px::INVALID_ACTOR_ID;
		}

		void YieldCurrentWorldFiber()
		{
			auto* local = CurrentShardLocal();
			if (!local || !local->scheduler || local->scheduler->Current() == 0)
				return;

			local->scheduler->YieldFiber();
		}


	} // anonymous namespace





	ServerWorld::ServerWorld(
		const WorldConfig& config,
		std::unique_ptr<IWorldContent> content,
		EnterWorldHandler enterWorld)
		: PhysicalWorld(config)
		, m_content(std::move(content))
		, m_enterWorld(std::move(enterWorld))
	{
	}

	ServerWorld::~ServerWorld() = default;

	bool ServerWorld::OnInitialize()
	{
		if (!PhysicalWorld::OnInitialize()) return false;

		if (auto shard = m_shard.lock())
			m_bridge = std::make_unique<ShardJobBridge>(*shard);

		if (!m_physics) return false;

		ClearActorDirectory();
		m_userToControlledEntity.clear();
		m_playerSpawnResults.clear();
		m_pendingPlayerSpawns.clear();
		m_metrics.Init(GetWorldId(), GetWorldShardIndex(GetWorldId()));

		m_physics->SetJobBridge(m_bridge.get());
		m_physics->Init();

		m_registry.ctx().emplace<ServerWorld*>(this);
		m_registry.ctx().emplace<TickCounter>().Init();
		m_registry.ctx().emplace<ServerInputSystem>(m_registry).Init();
		m_registry.ctx().emplace<ServerPhysicsSystem>(m_registry, m_physics.get()).Init();
		m_registry.ctx().emplace<ServerAoiSystem>(m_registry, m_physics.get(), m_metrics).Init();
		m_registry.ctx().emplace<ServerReplicationSystem>(m_registry, m_metrics).Init();

		BootstrapLevelActors();

		if (m_content)
		{
			if (!m_content->Initialize(*this))
			{
				m_content->Shutdown(*this);
				return false;
			}
			m_contentInitialized = true;
		}
		
		return true;
	}

	void ServerWorld::OnShutdown()
	{
		if (m_content && m_contentInitialized)
		{
			m_content->Shutdown(*this);
			m_contentInitialized = false;
		}

		Stop();
		ShutdownPhysicsWhenPipelineStops();
	}

	void ServerWorld::DispatchPhysicsEvents(std::span<const px::PhysicsEvent> events)
	{
		if (m_content && m_contentInitialized && !events.empty())
			m_content->OnPhysicsEvents(*this, events);
	}

	void ServerWorld::Start(uint64 dt_ns)
	{
		JAM_ASSERT(IsCurrentShardContext());
		StartTickPipeline({ DOMAIN_PHYSICS, GetWorldLocalIndex(GetWorldId()) }, dt_ns);
	}

	void ServerWorld::Resume(uint64 dt_ns)
	{
		Start(dt_ns);
	}

	void ServerWorld::Stop()
	{
		JAM_ASSERT(IsCurrentShardContext());
		StopTickPipeline();
	}

	bool ServerWorld::AddMember(WorldUserContext user)
	{
		const UserId userId = user.userId;
		if (!WorldMembershipHost::AddMember(std::move(user)))
			return false;

		auto* replication = m_registry.ctx().find<ServerReplicationSystem>();
		if (!replication || !replication->AttachUser(userId))
		{
			WorldMembershipHost::RemoveMember(userId);
			return false;
		}
		return true;
	}

	void ServerWorld::SuspendMemberReplication(UserId userId)
	{
		JAM_ASSERT(IsCurrentShardContext());
		if (!m_userContexts.contains(userId))
			return;
		if (auto* replication = m_registry.ctx().find<ServerReplicationSystem>())
			replication->SuspendUser(userId);
	}

	bool ServerWorld::ResumeMemberReplication(UserId userId)
	{
		JAM_ASSERT(IsCurrentShardContext());
		if (!m_userContexts.contains(userId))
			return false;

		if (auto* input = m_registry.ctx().find<ServerInputSystem>())
			input->RemoveUser(userId);
		if (auto* replication = m_registry.ctx().find<ServerReplicationSystem>())
			return replication->ResumeUserWithFullSync(userId);
		return false;
	}


	void ServerWorld::SendTo(Packet packet, UserId userId)
	{
		if (!packet.IsValid())
		{
			JAMNET_LOG_WARN_LOC("send packet is invalid");
			return;
		}

		const eProtocolType protocol = ResolveProtocol(packet);
		if (protocol == eProtocolType::NONE)
			return;

		const PacketHeaderView view = PacketHeaderView::Parse(packet->Head(), packet->Size());
		if (!view.IsValid())
			return;

		if (const auto it = m_userContexts.find(userId); it != m_userContexts.end())
		{
			if (!PostToSession(it->second.sessions, protocol, std::move(packet)))
			{
				JAMNET_LOG_WARN("[ServerWorldTx] failed to schedule packet. userId={}, type={}, id={}, channel={}",
					userId, static_cast<uint8>(view.Type()), view.Id(), static_cast<uint8>(view.Channel()));
			}
		}
	}

	void ServerWorld::Multicast(Packet packet)
	{
		if (!packet.IsValid())
			return;

		const eProtocolType protocol = ResolveProtocol(packet);
		if (protocol == eProtocolType::NONE)
			return;

		const PacketHeaderView view = PacketHeaderView::Parse(packet->Head(), packet->Size());
		if (!view.IsValid())
			return;

		for (const auto& p : m_userContexts | std::views::values)
		{
			if (!PostToSession(p.sessions, protocol, ClonePacket(packet)))
			{
				JAMNET_LOG_WARN_LOC("[ServerWorld] failed send packet");
			}
		}
	}


	void ServerWorld::HandleWorldPacket(UserId userId, Packet packet)
	{
		if (!m_userContexts.contains(userId))
			return;

		const PacketHeaderView view = PacketHeaderView::Parse(packet->Head(), packet->Size());
		if (!view.IsValid())
			return;

		switch (view.Id())
		{
		case CustomPacketId::INPUT:
		{
			ProcessGameInput(userId, view);
			break;
		}
		case CustomPacketId::BASELINE_ACK:
		{
			flatbuffers::Verifier verifier(view.Payload(), view.PayloadSize());
			if (!fb::VerifyfbBaselineAckBatchBuffer(verifier))
				break;
			const auto* batch = fb::GetfbBaselineAckBatch(view.Payload());
			if (!batch || batch->world_id() != GetWorldId() || batch->world_instance_id() != GetWorldInstance().instanceId.value)
				break;
			if (auto* replication = m_registry.ctx().find<ServerReplicationSystem>())
				replication->HandleBaselineFeedback(userId, *batch);
			break;
		}
		default: break;
		}
	}

	void ServerWorld::SpawnPlayerAsync(UserId userId, const WorldEventCorrelation& correlation, SpawnParams params,
		std::function<void(ActorId, ePlayerSpawnFailure)> onDone)
	{
		JAM_ASSERT(IsCurrentShardContext());

		if (IsPipelineTickInProgress())
		{
			const bool deferred = DeferUntilPipelineSafePoint(
				[this, userId, correlation, params = params, onDone = std::move(onDone)]() mutable
				{
					SpawnPlayerAsync(userId, correlation, params, std::move(onDone));
				});
			JAM_ASSERT(deferred);
			return;
		}

		auto complete = [&onDone](ActorId actorId, ePlayerSpawnFailure failure)
			{
				if (onDone)
					onDone(actorId, failure);
			};

		const auto* replication = m_registry.ctx().find<ServerReplicationSystem>();
		if (userId == 0 || !correlation.world.IsValid() || correlation.world != GetWorldRef()
			|| !m_userContexts.contains(userId) || !replication
			|| params.controller != userId || params.desc.IsRigid())
		{
			complete(ActorId::Invalid(), ePlayerSpawnFailure::InvalidCorrelation);
			return;
		}

		if (const auto it = m_playerSpawnResults.find(userId); it != m_playerSpawnResults.end())
		{
			if (it->second.clientRequestId == params.clientRequestId)
				complete(it->second.actorId, ePlayerSpawnFailure::None);
			else
				complete(ActorId::Invalid(), ePlayerSpawnFailure::AlreadySpawned);
			return;
		}
		if (auto it = m_pendingPlayerSpawns.find(userId); it != m_pendingPlayerSpawns.end())
		{
			if (it->second.clientRequestId == params.clientRequestId)
			{
				if (onDone)
					it->second.completions.push_back(std::move(onDone));
			}
			else
			{
				complete(ActorId::Invalid(), ePlayerSpawnFailure::AlreadySpawned);
			}
			return;
		}

		if (!replication->IsAwaitingPlayer(userId))
		{
			complete(ActorId::Invalid(), ePlayerSpawnFailure::InvalidCorrelation);
			return;
		}

		const ClientRequestId clientRequestId = params.clientRequestId;
		const ActorId actorId = SpawnActor(params);
		if (!actorId.IsValid())
		{
			complete(ActorId::Invalid(), ePlayerSpawnFailure::SpawnFailed);
			return;
		}

		const entt::entity entity = ResolveActor(actorId);
		if (entity == entt::null || !m_registry.valid(entity))
		{
			complete(ActorId::Invalid(), ePlayerSpawnFailure::SpawnFailed);
			return;
		}

		EnsureUserAoiRegistration(userId);

		PendingPlayerSpawn pending
		{
			.correlation = correlation,
			.clientRequestId = clientRequestId,
			.actorId = actorId,
			.entity = entity,
			.deadlineNs = NOW_NS() + 10_s,
		};
		if (onDone)
			pending.completions.push_back(std::move(onDone));
		m_pendingPlayerSpawns.emplace(userId, std::move(pending));
	}

	bool ServerWorld::DespawnPlayer(UserId userId, const WorldEventCorrelation& correlation, ActorId actorId)
	{
		JAM_ASSERT(IsCurrentShardContext());
		const entt::entity player = ResolveActor(actorId);
		const bool valid = userId != 0
			&& correlation.world == GetWorldRef()
			&& m_userContexts.contains(userId)
			&& player != entt::null
			&& GetControlledEntity(userId) == player;
		return valid && DespawnActor(actorId, userId);
	}

	bool ServerWorld::RestorePlayerControl(UserId userId, ActorId actorId)
	{
		JAM_ASSERT(IsCurrentShardContext());
		const entt::entity player = ResolveActor(actorId);
		if (userId == kInvalidUserId || !m_userContexts.contains(userId)
			|| player == entt::null || !m_registry.valid(player))
		{
			return false;
		}

		ApplyInitialControl(player, userId);
		EnsureUserAoiRegistration(userId);
		if (auto* replication = m_registry.ctx().find<ServerReplicationSystem>())
		{
			replication->MarkActorDirty(player, true);
			if (replication->IsAwaitingPlayer(userId) && !replication->BeginInitialSync(userId))
				return false;
		}
		return GetControlledEntity(userId) == player;
	}

	void ServerWorld::PrepareMemberContent(const ServerWorldMemberContentContext& context, IWorldContent::PrepareMemberCompletion completion)
	{
		JAM_ASSERT(IsCurrentShardContext());
		if (!m_content || !m_userContexts.contains(context.userId))
		{
			if (completion) completion(!m_content);
			return;
		}
		m_content->PrepareMemberContent(*this, context, std::move(completion));
	}

	void ServerWorld::RollbackMemberContent(UserId userId, WorldTransitionToken transitionToken)
	{
		JAM_ASSERT(IsCurrentShardContext());
		if (m_content)
			m_content->RollbackMemberContent(*this, userId, transitionToken);
	}

	bool ServerWorld::CommitMemberLeave(UserId userId, WorldTransitionToken transitionToken)
	{
		JAM_ASSERT(IsCurrentShardContext());
		return !m_content || m_content->CommitMemberLeave(*this, userId, transitionToken);
	}

	bool ServerWorld::RestoreMemberContent(const WorldUserContext& user, WorldTransitionToken transitionToken)
	{
		JAM_ASSERT(IsCurrentShardContext());
		return !m_content || m_content->RestoreMemberContent(*this, user, transitionToken);
	}

	entt::entity ServerWorld::GetControlledEntity(UserId userId) const
	{
		if (userId == 0)
			return entt::null;

		if (auto it = m_userToControlledEntity.find(userId); it != m_userToControlledEntity.end())
			return it->second;
		return entt::null;
	}

	bool ServerWorld::RequestEnterWorld(UserId userId, EnterWorldRequest request)
	{
		JAM_ASSERT(IsCurrentShardContext());

		const auto member = m_userContexts.find(userId);
		if (!m_enterWorld || member == m_userContexts.end() || !request.IsValid())
			return false;

		request.expectedMainRevision = member->second.mainRevision;
		m_enterWorld(userId, request);
		return true;
	}



	void ServerWorld::Tick()
	{
		if (!m_registry.ctx().contains<TickCounter>() 
			|| !m_registry.ctx().contains<ServerInputSystem>() 
			|| !m_registry.ctx().contains<ServerPhysicsSystem>()
			|| !m_registry.ctx().contains<ServerAoiSystem>()
			|| !m_registry.ctx().contains<ServerReplicationSystem>())
			return;

		std::array<uint64, 6> phases{};
		const uint64 tickStartNs = NOW_NS();
		m_registry.ctx().get<TickCounter>().Tick();

		uint64 phaseStartNs = NOW_NS();
		m_registry.ctx().get<ServerInputSystem>().Tick();
		phases[1] = NOW_NS() - phaseStartNs;

		phaseStartNs = NOW_NS();
		m_registry.ctx().get<ServerPhysicsSystem>().Tick();
		phases[2] = NOW_NS() - phaseStartNs;
		YieldCurrentWorldFiber();

		phaseStartNs = NOW_NS();
		m_registry.ctx().get<ServerAoiSystem>().Tick();
		phases[3] = NOW_NS() - phaseStartNs;
		YieldCurrentWorldFiber();

		phaseStartNs = NOW_NS();
		m_registry.ctx().get<ServerReplicationSystem>().Tick();
		phases[4] = NOW_NS() - phaseStartNs;

		phaseStartNs = NOW_NS();
		FinalizePendingPlayerSpawns();
		const uint64 profileNowNs = NOW_NS();
		phases[5] = profileNowNs - phaseStartNs;
		phases[0] = profileNowNs - tickStartNs;
		m_metrics.RecordTick(phases, m_userContexts.size(), profileNowNs);
		SubmitWorldMetrics(profileNowNs);
	}

	void ServerWorld::SubmitWorldMetrics(uint64 nowNs)
	{
		if (!m_metrics.IsReadyToSubmit(nowNs))
			return;

		m_metrics.SubmitSnapshot(nowNs);
		m_metrics.Reset();
	}

	void ServerWorld::FinalizePendingPlayerSpawns()
	{
		if (m_pendingPlayerSpawns.empty())
			return;

		auto* aoi = m_registry.ctx().find<ServerAoiSystem>();
		auto* replication = m_registry.ctx().find<ServerReplicationSystem>();
		if (!aoi || !replication)
			return;

		const uint64 nowNs = NOW_NS();
		std::vector<uint64> readyUsers;
		std::vector<uint64> failedUsers;
		readyUsers.reserve(m_pendingPlayerSpawns.size());
		failedUsers.reserve(m_pendingPlayerSpawns.size());

		for (const auto& [userId, pending] : m_pendingPlayerSpawns)
		{
			if (!m_userContexts.contains(userId)
				|| pending.correlation.world != GetWorldRef()
				|| !replication->IsAwaitingPlayer(userId)
				|| pending.entity == entt::null
				|| !m_registry.valid(pending.entity)
				|| ResolveActor(pending.actorId) != pending.entity)
			{
				failedUsers.push_back(userId);
				continue;
			}

			EnsureUserAoiRegistration(userId);

			const auto* control = m_registry.try_get<ControlTag>(pending.entity);
			const bool playerReady = m_registry.all_of<PhysicsSpawnedTag>(pending.entity)
				&& control && control->userId == userId
				&& GetControlledEntity(userId) == pending.entity
				&& aoi->IsActorRegistered(pending.entity)
				&& aoi->IsUserReady(userId);

			if (playerReady)
				readyUsers.push_back(userId);
			else if (nowNs >= pending.deadlineNs)
				failedUsers.push_back(userId);
		}

		for (const uint64 userId : readyUsers)
		{
			auto it = m_pendingPlayerSpawns.find(userId);
			if (it == m_pendingPlayerSpawns.end())
				continue;

			const ActorId actorId = it->second.actorId;
			if (replication->BeginInitialSync(userId))
				CompletePendingPlayerSpawn(userId, actorId, ePlayerSpawnFailure::None);
			else
				CompletePendingPlayerSpawn(userId, ActorId::Invalid(), ePlayerSpawnFailure::SpawnFailed);
		}

		for (const uint64 userId : failedUsers)
			CompletePendingPlayerSpawn(userId, ActorId::Invalid(), ePlayerSpawnFailure::SpawnFailed);
	}

	void ServerWorld::CompletePendingPlayerSpawn(UserId userId, ActorId actorId, ePlayerSpawnFailure failure)
	{
		auto it = m_pendingPlayerSpawns.find(userId);
		if (it == m_pendingPlayerSpawns.end())
			return;

		PendingPlayerSpawn pending = std::move(it->second);
		m_pendingPlayerSpawns.erase(it);

		if (failure == ePlayerSpawnFailure::None && actorId.IsValid())
		{
			m_playerSpawnResults.insert_or_assign(userId, PlayerSpawnResult
				{
					.clientRequestId = pending.clientRequestId,
					.actorId = actorId,
				});
		}
		else
		{
			actorId = ActorId::Invalid();
			if (pending.actorId.IsValid())
				DespawnActor(pending.actorId, userId);
		}

		for (auto& completion : pending.completions)
			if (completion) completion(actorId, failure);
	}

	void ServerWorld::OnUserEntered(UserId userId)
	{
		EnsureUserAoiRegistration(userId);
	}

	void ServerWorld::OnUserLeft(UserId userId)
	{
		if (auto* input = m_registry.ctx().find<ServerInputSystem>())
			input->RemoveUser(userId);

		if (auto it = m_userToControlledEntity.find(userId); it != m_userToControlledEntity.end())
		{
			const entt::entity controlled = it->second;
			m_userToControlledEntity.erase(it);
			if (m_registry.valid(controlled) && m_registry.all_of<ControlTag>(controlled))
			{
				auto& control = m_registry.get<ControlTag>(controlled);
				if (control.userId == userId)
				{
					control.userId = 0;
					m_registry.remove<px::CharacterMotorInput>(controlled);
					if (auto* replication = m_registry.ctx().find<ServerReplicationSystem>())
						replication->MarkActorDirty(controlled, true);
				}
			}
		}

		if (auto* repl = m_registry.ctx().find<ServerReplicationSystem>())
			repl->OnUserLeave(userId);

		if (auto* aoi = m_registry.ctx().find<ServerAoiSystem>())
			aoi->OnUserLeave(userId);

		if (auto it = m_pendingPlayerSpawns.find(userId); it != m_pendingPlayerSpawns.end())
		{
			PendingPlayerSpawn pending = std::move(it->second);
			m_pendingPlayerSpawns.erase(it);
			if (pending.actorId.IsValid())
				DespawnActor(pending.actorId, userId);
			for (auto& completion : pending.completions)
				if (completion) completion(ActorId::Invalid(), ePlayerSpawnFailure::SpawnFailed);
		}
		m_playerSpawnResults.erase(userId);
	}

	void ServerWorld::BootstrapLevelActors()
	{
		if (!m_physics || m_actorLevels.instances.empty())
			return;

		for (const ActorLevelInstanceData& instance : m_actorLevels.instances)
		{
			const ActorArchetypeData* actorArchetype = m_actorArchetypes.Find(instance.actorArchetype);
			if (!actorArchetype || !actorArchetype->AllowsSpawn(eActorSpawnSource::Level)
				|| !IsValidAssetKey(actorArchetype->key) || !IsValidAssetKey(actorArchetype->physicsArchetype))
				continue;

			const px::PhysicsArchetypeKey physicsArchetypeKey = actorArchetype->physicsArchetype;
			if (!IsValidAssetKey(physicsArchetypeKey))
				continue;

			const px::eBodyType bodyType = m_physics->FindBodyType(physicsArchetypeKey);
			if (bodyType == px::eBodyType::None)
				continue;

			const ActorId actorId = ActorId(instance.actorId);
			if (!actorId.IsValid() || ResolveActor(actorId) != entt::null)
			{
				JAMNET_LOG_WARN("[ServerPhysicalWorld::BootstrapLevelActors] invalid or duplicate actorId={}", instance.actorId);
				continue;
			}

			const entt::entity e = m_registry.create();

			if (!m_actorDirectory.Bind(actorId, e))
			{
				m_registry.destroy(e);
				continue;
			}
			m_registry.emplace<ActorId>(e, actorId);

			px::SpawnDesc desc{};
			desc.archetype = physicsArchetypeKey;
			desc.pose = instance.pose;
			desc.spawnSrc = px::eSpawnSource::Level;
			desc.overrides = (bodyType == px::eBodyType::Character)
				? std::variant<px::RigidSpawnOverrides, px::CharacterSpawnOverrides>(px::CharacterSpawnOverrides{})
				: std::variant<px::RigidSpawnOverrides, px::CharacterSpawnOverrides>(px::RigidSpawnOverrides{});

			m_registry.emplace<ActorBodyType>(e, ActorBodyType{ bodyType });
			m_registry.emplace<ActorArchetypeRef>(e, ActorArchetypeRef{ actorArchetype->key });
			m_registry.emplace<PhysicsArchetypeRef>(e, PhysicsArchetypeRef{ physicsArchetypeKey });
			m_registry.emplace<OwnershipTag>(e);
			m_registry.emplace<ControlTag>(e);

			if (bodyType == px::eBodyType::Character)
			{
				px::CharacterState state{};
				state.pos = instance.pose.p;
				m_registry.emplace<CharAuthorityState>(e, state);
			}
			else
			{
				px::RigidState state{};
				state.pose = instance.pose;
				m_registry.emplace<RigidAuthorityState>(e, state);
			}

			m_registry.emplace<NewlyCreatedTag>(e);
			if (!actorArchetype->allowReplication)
				m_registry.emplace<ReplicationDisabledTag>(e);
			else if (m_physics->FindMotionType(physicsArchetypeKey) == px::eMotionType::Static)
				m_registry.emplace<ReplicationStaticTag>(e);

			m_registry.ctx().get<ServerPhysicsSystem>().SpawnActor(e, desc);
			if (!m_registry.valid(e))
			{
				m_actorDirectory.Unbind(actorId);
				continue;
			}

			if (!m_registry.all_of<PhysicsSpawnedTag>(e))
			{
				ReleaseActorId(e);
				m_registry.destroy(e);
				continue;
			}

			if (!m_registry.all_of<ReplicationDisabledTag>(e) && m_registry.all_of<PhysicsSpawnedTag>(e))
			{
				if (auto* aoi = m_registry.ctx().find<ServerAoiSystem>())
					aoi->OnActorSpawned(e);
			}
		}
		JAM_ASSERT(ValidateActorIdentities());
	}

	ActorId ServerWorld::SpawnActor(SpawnParams params)
	{
		JAM_ASSERT(IsCurrentShardContext());
		JAM_ASSERT(!IsPipelineTickInProgress());
		if (IsPipelineTickInProgress())
			return ActorId::Invalid();

		if (!IsValidAssetKey(params.actorArchetypeKey))
			return ActorId::Invalid();

		const ActorArchetypeData* actorArchetype = m_actorArchetypes.Find(params.actorArchetypeKey);
		if (!actorArchetype || !actorArchetype->AllowsSpawn(eActorSpawnSource::Runtime)
			|| !IsValidAssetKey(actorArchetype->physicsArchetype))
			return ActorId::Invalid();

		if (IsValidAssetKey(params.desc.archetype) && params.desc.archetype != actorArchetype->physicsArchetype)
			return ActorId::Invalid();
		params.desc.archetype = actorArchetype->physicsArchetype;

		if (!IsValidAssetKey(params.desc.archetype))
			return ActorId::Invalid();

		const entt::entity e = m_registry.create();
		const ActorId actorId = AllocateActorId(e);
		if (!actorId.IsValid())
		{
			m_registry.destroy(e);
			return ActorId::Invalid();
		}
		const px::eBodyType body = params.desc.IsRigid() ? px::eBodyType::Rigid : px::eBodyType::Character;

		m_registry.emplace<ActorBodyType>(e, ActorBodyType{ body });
		m_registry.emplace<ActorArchetypeRef>(e, ActorArchetypeRef{ params.actorArchetypeKey });
		m_registry.emplace<PhysicsArchetypeRef>(e, PhysicsArchetypeRef{ params.desc.archetype });
		m_registry.emplace<OwnershipTag>(e, OwnershipTag{ params.owner });
		m_registry.emplace<ControlTag>(e);

		if (!actorArchetype->allowReplication)
			m_registry.emplace<ReplicationDisabledTag>(e);
		else if (m_physics->FindMotionType(params.desc.archetype) == px::eMotionType::Static)
			m_registry.emplace<ReplicationStaticTag>(e);
		
		if (body == px::eBodyType::Rigid)
		{
			m_registry.emplace<RigidAuthorityState>(e);
		}
		else
		{
			m_registry.emplace<CharAuthorityState>(e);
		}

		m_registry.emplace<NewlyCreatedTag>(e);
		m_registry.emplace<ClientRequestCorrelation>(e, ClientRequestCorrelation{ params.clientRequestId });


		if (params.controller != 0)
			ApplyInitialControl(e, params.controller);

		params.desc.targetActorId = BindingTarget(m_registry, e, params.targetActorId, m_actorDirectory);

		m_registry.ctx().get<ServerPhysicsSystem>().SpawnActor(e, params.desc);
		if (!m_registry.valid(e))
		{
			m_actorDirectory.Release(actorId);
			if (params.controller != 0)
			{
				auto it = m_userToControlledEntity.find(params.controller);
				if (it != m_userToControlledEntity.end() && it->second == e)
					m_userToControlledEntity.erase(it);
			}
			return ActorId::Invalid();
		}

		if (!m_registry.all_of<PhysicsSpawnedTag>(e))
		{
			ReleaseActorId(e);
			m_registry.destroy(e);
			if (params.controller != 0)
			{
				auto it = m_userToControlledEntity.find(params.controller);
				if (it != m_userToControlledEntity.end() && it->second == e)
					m_userToControlledEntity.erase(it);
			}
			return ActorId::Invalid();
		}

		if (!m_registry.all_of<ReplicationDisabledTag>(e) && m_registry.all_of<PhysicsSpawnedTag>(e))
		{
			if (auto* aoi = m_registry.ctx().find<ServerAoiSystem>())
				aoi->OnActorSpawned(e);
		}


		JAM_ASSERT(ValidateActorIdentities());
		return actorId;
	}

	void ServerWorld::SpawnActorAsync(SpawnParams params, std::function<void(ActorId)> onDone)
	{
		JAM_ASSERT(IsCurrentShardContext());
		if (IsPipelineTickInProgress())
		{
			const bool deferred = DeferUntilPipelineSafePoint(
				[this, params = params, onDone = std::move(onDone)]() mutable
				{
					SpawnActorAsync(params, std::move(onDone));
				});
			JAM_ASSERT(deferred);
			return;
		}

		const ActorId actorId = SpawnActor(params);
		if (onDone)
			onDone(actorId);
	}

	bool ServerWorld::DespawnActor(ActorId actorId, UserId requester)
	{
		JAM_ASSERT(IsCurrentShardContext());
		if (IsPipelineTickInProgress())
		{
			const bool deferred = DeferUntilPipelineSafePoint(
				[this, actorId, requester]()
				{
					DespawnActor(actorId, requester);
				});
			JAM_ASSERT(deferred);
			return deferred;
		}

		const entt::entity targetEntity = ResolveActor(actorId);

		if (targetEntity == entt::null)
			return false;

		if (auto* ownership = m_registry.try_get<OwnershipTag>(targetEntity))
		{
			if (requester != kInvalidUserId && ownership->userId != requester)
				return false;
		}

		if (auto* repl = m_registry.ctx().find<ServerReplicationSystem>())
			repl->OnActorDestroyed(targetEntity);
		if (auto* aoi = m_registry.ctx().find<ServerAoiSystem>())
			aoi->OnActorDestroyed(targetEntity);

		if (const auto* control = m_registry.try_get<ControlTag>(targetEntity))
		{
			if (control->userId != 0)
			{
				auto it = m_userToControlledEntity.find(control->userId);
				if (it != m_userToControlledEntity.end() && it->second == targetEntity)
					m_userToControlledEntity.erase(it);
			}
		}

		for (auto it = m_playerSpawnResults.begin(); it != m_playerSpawnResults.end();)
		{
			if (it->second.actorId == actorId)
				it = m_playerSpawnResults.erase(it);
			else
				++it;
		}

		if (m_registry.all_of<PhysicsSpawnedTag>(targetEntity))
		{
			if (auto* phys = m_registry.ctx().find<ServerPhysicsSystem>())
			{
				phys->DespawnActor(targetEntity);
			}
			else
			{
				return false;
			}
		}

		ReleaseActorId(targetEntity);
		m_registry.destroy(targetEntity);

		JAM_ASSERT(ValidateActorIdentities());
		return true;
	}

	void ServerWorld::DespawnActorAsync(ActorId actorId, UserId requester, std::function<void(bool)> onDone)
	{
		JAM_ASSERT(IsCurrentShardContext());
		if (IsPipelineTickInProgress())
		{
			const bool deferred = DeferUntilPipelineSafePoint(
				[this, actorId, requester, onDone = std::move(onDone)]() mutable
				{
					DespawnActorAsync(actorId, requester, std::move(onDone));
				});
			JAM_ASSERT(deferred);
			return;
		}

		const bool succeeded = DespawnActor(actorId, requester);
		if (onDone)
			onDone(succeeded);
	}

	void ServerWorld::EnsureUserAoiRegistration(UserId userId)
	{
		if (userId == kInvalidUserId || !m_userContexts.contains(userId))
			return;

		const entt::entity controlled = GetControlledEntity(userId);
		if (controlled == entt::null || !m_registry.valid(controlled)
			|| !m_registry.all_of<PhysicsSpawnedTag>(controlled))
			return;

		if (auto* aoi = m_registry.ctx().find<ServerAoiSystem>(); aoi && !aoi->GetState(userId))
			aoi->OnUserEnter(userId);
	}

	void ServerWorld::ApplyInitialControl(entt::entity entity, UserId userId)
	{
		if (userId == 0 || !m_registry.valid(entity))
			return;

		const entt::entity previous = GetControlledEntity(userId);
		if (previous != entt::null && previous != entity && m_registry.valid(previous) && m_registry.all_of<ControlTag>(previous))
		{
			auto& control = m_registry.get<ControlTag>(previous);
			if (control.userId == userId)
			{
				control.userId = 0;
				m_registry.remove<px::CharacterMotorInput>(previous);
				if (auto* replication = m_registry.ctx().find<ServerReplicationSystem>())
					replication->MarkActorDirty(previous, true);
			}
		}

		m_registry.emplace_or_replace<ControlTag>(entity, ControlTag{ userId });
		if (const auto* body = m_registry.try_get<ActorBodyType>(entity); body && body->body == px::eBodyType::Character)
			m_registry.emplace_or_replace<px::CharacterMotorInput>(entity);

		m_userToControlledEntity[userId] = entity;
		if (auto* aoi = m_registry.ctx().find<ServerAoiSystem>())
			aoi->OnControlledActorChanged(userId);

		if (auto* replication = m_registry.ctx().find<ServerReplicationSystem>())
			replication->MarkActorDirty(entity, true);
	}

	void ServerWorld::ProcessGameInput(UserId userId, const PacketHeaderView& pkt)
	{
		flatbuffers::Verifier verfier(pkt.Payload(), pkt.PayloadSize());
		if (!fb::VerifyfbCharacterControlCommandBuffer(verfier)) return;

		const auto* wireCommand = fb::GetfbCharacterControlCommand(pkt.Payload());
		if (!wireCommand) return;
		if (userId == 0 || !m_userContexts.contains(userId) || GetControlledEntity(userId) == entt::null)
			return;

		CharacterControlCommand cmd{};
		cmd.sequence				 = wireCommand->sequence();
		cmd.intent.controlRevision   = wireCommand->control_revision();
		cmd.intent.moveReferenceYaw	 = wireCommand->move_reference_yaw();
		cmd.intent.viewYaw			 = wireCommand->view_yaw();
		cmd.intent.viewPitch		 = wireCommand->view_pitch();
		cmd.intent.viewPolicy		 = static_cast<eCharacterViewPolicy>(wireCommand->view_policy());
		cmd.intent.continuousActions = wireCommand->continuous_action_flags();
		cmd.intent.edgeActions		 = wireCommand->edge_action_flags();
		
		switch (wireCommand->locomotion_kind())
		{
		case fb::fbLocomotionKind_Directional: 
			cmd.intent.locomotion = DirectionalMoveIntent{ wireCommand->local_x(), wireCommand->local_y() }; 
			break;

		case fb::fbLocomotionKind_MoveToPosition: 
			cmd.intent.locomotion = MoveToPositionIntent{ px::Vec3(wireCommand->target_x(), wireCommand->target_y(), wireCommand->target_z()) }; 
			break;

		case fb::fbLocomotionKind_FollowActor: 
			cmd.intent.locomotion = FollowActorIntent{ ActorId(wireCommand->target_actor_id()) }; 
			break;

		default: 
			cmd.intent.locomotion = StopMovementIntent{}; 
			break;
		}

		if (m_registry.ctx().contains<ServerInputSystem>())
		{
			m_registry.ctx().get<ServerInputSystem>().EnqueueInput(userId, cmd);
		}
	}
}
