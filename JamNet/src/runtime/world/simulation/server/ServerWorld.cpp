#include "pch.h"

#include "jamnet/core/net/PacketBuilder.h"
#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/core/executor/GlobalExecutor.h"
#include "jamnet/core/utils/ScopedTimer.h"

#include "jamnet/runtime/world/simulation/server/ServerWorld.h"
#include "jamnet/core/executor/ShardExecutor.h"
#include "jamnet/runtime/world/simulation/common/ActorComponents.h"
#include "jamnet/runtime/world/simulation/common/WorldContext.h"
#include "jamnet/runtime/world/simulation/server/ServerActorLifecycleSystem.h"
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

		bool IsValidPlayerSpawnRequest(const ServerWorld& world, UserId userId, const WorldEventCorrelation& correlation, const SpawnParams& params, bool canExecute, bool isMember)
		{
			return canExecute
				&& isMember
				&& userId != kInvalidUserId
				&& correlation.world == world.GetWorldRef()
				&& correlation.world.IsValid()
				&& params.clientRequestId != kInvalidClientRequestId
				&& !params.desc.IsRigid();
		}

		bool IsValidPlayerDespawnRequest(const ServerWorld& world, UserId userId, const WorldEventCorrelation& correlation, entt::entity player, bool canExecute, bool isMember)
		{
			return canExecute
				&& isMember
				&& userId != kInvalidUserId
				&& correlation.world == world.GetWorldRef()
				&& player != entt::null
				&& world.GetPlayerEntity(userId) == player;
		}

		bool IsValidPlayerRestoreRequest(UserId userId, entt::entity player, const entt::registry& registry, bool canExecute, bool isMember)
		{
			return canExecute
				&& isMember
				&& userId != kInvalidUserId
				&& player != entt::null
				&& registry.valid(player)
				&& registry.all_of<ActorId, PhysicsSpawnedTag>(player);
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

	px::eMotionType ServerWorld::FindActorMotionType(px::PhysicsArchetypeKey archetype) const
	{
		return m_physics ? m_physics->FindMotionType(archetype) : px::eMotionType::None;
	}

	px::eBodyType ServerWorld::FindActorBodyType(px::PhysicsArchetypeKey archetype) const
	{
		return m_physics ? m_physics->FindBodyType(archetype) : px::eBodyType::None;
	}

	bool ServerWorld::OnInitialize()
	{
		if (!PhysicalWorld::OnInitialize()) return false;

		if (!m_physics) return false;

		ClearActorDirectory();
		m_userToPlayerEntity.clear();
		m_metrics.Init(GetWorldId(), GetWorldShardIndex(GetWorldId()));

		m_physics->SetConfig(px::PhysicsConfig{
			.enableReplay = false,
		});
		m_physics->Init();


		m_tickCounter.Init();

		m_actorSystem		= std::make_unique<ServerActorLifecycleSystem>(*this, m_registry, m_actorDirectory, m_actorArchetypes);
		m_inputSystem		= std::make_unique<ServerInputSystem>(*this, m_registry);
		m_physicsSystem		= std::make_unique<ServerPhysicsSystem>(*this, m_registry, m_physics.get());
		m_aoiSystem			= std::make_unique<ServerAoiSystem>(*this, m_registry, m_physics.get(), m_metrics, m_tickCounter);
		m_replicationSystem = std::make_unique<ServerReplicationSystem>(*this, m_registry, m_metrics);

		m_inputSystem->Init();
		m_physicsSystem->Init();
		m_aoiSystem->Init();
		m_replicationSystem->Init();

		BootstrapLevelActors();

		if (m_content)
		{
			if (!m_content->OnInitialize(*this))
			{
				m_content->OnClose(*this);
				return false;
			}
			m_contentInitialized = true;
		}

		return true;
	}

	void ServerWorld::OnCloseStarted()
	{
		if (m_content && m_contentInitialized)
		{
			m_content->OnClose(*this);
			m_contentInitialized = false;
		}

		Stop();
		ShutdownPhysicsAfterTick();
	}

	void ServerWorld::DispatchPhysicsEvents(std::span<const px::PhysicsEvent> events)
	{
		if (m_content && m_contentInitialized && !events.empty())
			m_content->OnPhysicsEvents(*this, events);
	}

	void ServerWorld::Start(Duration dt)
	{
		JAM_ASSERT(IsCurrentShardContext());
		StartDomainTick({ DOMAIN_PHYSICS, GetWorldLocalIndex(GetWorldId()) }, dt);
	}

	void ServerWorld::Resume(Duration dt)
	{
		Start(dt);
	}

	void ServerWorld::Stop()
	{
		JAM_ASSERT(IsCurrentShardContext());
		StopDomainTick();
	}

	bool ServerWorld::OnUserEntered(UserId userId)
	{
		return m_replicationSystem->AttachUser(userId);
	}

	void ServerWorld::SuspendMemberReplication(UserId userId)
	{
		JAM_ASSERT(IsCurrentShardContext());
		if (!m_userContexts.contains(userId))
			return;
		m_replicationSystem->SuspendUser(userId);
	}

	bool ServerWorld::ResumeMemberReplication(UserId userId)
	{
		JAM_ASSERT(IsCurrentShardContext());
		if (!m_userContexts.contains(userId))
			return false;

		m_inputSystem->ResetInputStream(userId);
		return m_replicationSystem->ResumeUserWithFullSync(userId, GetPlayerEntity(userId) != entt::null);
	}


	void ServerWorld::SendTo(Packet packet, UserId userId)
	{
		TrySendTo(std::move(packet), userId);
	}

	bool ServerWorld::TrySendTo(Packet packet, UserId userId)
	{
		if (!packet.IsValid())
		{
			JAM_LOG_WARN_LOC("send packet is invalid");
			return false;
		}

		const eProtocolType protocol = ResolveProtocol(packet);
		if (protocol == eProtocolType::NONE)
			return false;

		const PacketHeaderView view = PacketHeaderView::Parse(packet->Head(), packet->Size());
		if (!view.IsValid())
			return false;

		if (const auto it = m_userContexts.find(userId); it != m_userContexts.end())
		{
			return PostToSession(it->second.sessions, protocol, std::move(packet));
		}
		return false;
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
				JAM_LOG_WARN_LOC("[ServerWorld] failed send packet");
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
			m_replicationSystem->HandleBaselineFeedback(userId, *batch, m_aoiSystem->GetState(userId));
			break;
		}
		default: break;
		}
	}

	void ServerWorld::SpawnPlayerAsync(UserId userId, const WorldEventCorrelation& correlation, SpawnParams params, std::function<void(ActorId, ePlayerSpawnFailure)> onDone)
	{
		JAM_ASSERT(IsCurrentShardContext());
		JAM_ASSERT(!IsTickInProgress());

		auto complete = [&onDone](ActorId actorId, ePlayerSpawnFailure failure)
			{
				if (onDone) onDone(actorId, failure);
			};

		if (!IsValidPlayerSpawnRequest(*this, userId, correlation, params,
			!IsTickInProgress() && m_alive.load(std::memory_order_acquire), m_userContexts.contains(userId)))
		{
			complete(ActorId::Invalid(), ePlayerSpawnFailure::InvalidCorrelation);
			return;
		}

		if (GetPlayerEntity(userId) != entt::null)
		{
			complete(ActorId::Invalid(), ePlayerSpawnFailure::AlreadySpawned);
			return;
		}

		if (!m_replicationSystem->IsAwaitingPlayer(userId) && !m_replicationSystem->IsSuspended(userId))
		{
			complete(ActorId::Invalid(), ePlayerSpawnFailure::InvalidCorrelation);
			return;
		}

		params.owner = userId;
		const auto [actorId, entity] = ExecuteSpawnActor(params, true);
		if (!actorId.IsValid())
		{
			complete(ActorId::Invalid(), ePlayerSpawnFailure::SpawnFailed);
			return;
		}

		if (!BindPlayer(userId, entity))
		{
			ExecuteDespawnActor(userId, actorId);
			complete(ActorId::Invalid(), ePlayerSpawnFailure::SpawnFailed);
			return;
		}

		complete(actorId, ePlayerSpawnFailure::None);
	}

	void ServerWorld::DespawnPlayerAsync(UserId userId, const WorldEventCorrelation& correlation, ActorId actorId, std::function<void(bool)> onDone)
	{
		JAM_ASSERT(IsCurrentShardContext());
		JAM_ASSERT(!IsTickInProgress());

		const entt::entity player = ResolveActor(actorId);
		const bool valid =
			IsValidPlayerDespawnRequest(*this, userId, correlation, player, !IsTickInProgress() && m_alive.load(std::memory_order_acquire), m_userContexts.contains(userId));
		const bool succeeded = valid && ExecuteDespawnActor(userId, actorId);

		if (onDone) onDone(succeeded);
	}

	bool ServerWorld::RestorePlayerBinding(UserId userId, ActorId actorId)
	{
		JAM_ASSERT(IsCurrentShardContext());
		JAM_ASSERT(!IsTickInProgress());

		const entt::entity player = ResolveActor(actorId);
		if (!IsValidPlayerRestoreRequest(userId, player, m_registry, !IsTickInProgress() && m_alive.load(std::memory_order_acquire), m_userContexts.contains(userId)))
			return false;

		const entt::entity currentPlayer = GetPlayerEntity(userId);
		if (currentPlayer != entt::null && currentPlayer != player)
			return false;

		return BindPlayer(userId, player, currentPlayer == player);
	}


	entt::entity ServerWorld::GetPlayerEntity(UserId userId) const
	{
		if (userId == 0)
			return entt::null;

		if (auto it = m_userToPlayerEntity.find(userId); it != m_userToPlayerEntity.end())
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

		// Portal callbacks run inside Tick. Start the transition after Tick returns.
		return Post(Job([this, userId, request = std::move(request)]()
			{
				const auto current = m_userContexts.find(userId);
				if (!m_alive.load(std::memory_order_acquire)
					|| current == m_userContexts.end()
					|| current->second.mainRevision != request.expectedMainRevision)
				{
					return;
				}

				m_enterWorld(userId, request);
			}, eJobPriority::Control));
	}


	void ServerWorld::Tick()
	{
		JAM_ASSERT(IsCurrentShardContext());

		std::array<uint64, 6> phases{};
		const uint64 tickStartNs = NOW().Ns();
		m_tickCounter.Tick();

		uint64 phaseStartNs = NOW().Ns();

		m_inputSystem->Tick();
		phases[1] = NOW().Ns() - phaseStartNs;

		phaseStartNs = NOW().Ns();
		m_physicsSystem->Tick();
		m_inputSystem->MarkInputApplied(m_physicsSystem->GetLastAppliedInputUsers());
		const auto activeEntities = m_physicsSystem->GetLastActiveEntities();
		phases[2] = NOW().Ns() - phaseStartNs;

		phaseStartNs = NOW().Ns();
		m_aoiSystem->Tick(activeEntities);
		phases[3] = NOW().Ns() - phaseStartNs;

		phaseStartNs = NOW().Ns();
		m_replicationSystem->BeginTick(activeEntities);
		for (const auto& userId : m_userContexts | std::views::keys)
		{
			m_replicationSystem->ReplicateUser(ReplicationUserInput{
				.userId					= userId,
				.appliedInputSequence   = m_inputSystem->LastAppliedSeq(userId),
				.appliedControlRevision = m_inputSystem->LastAppliedControlRevision(userId),
				.aoiState				= m_aoiSystem->GetState(userId)
			}, m_tickCounter.tick);
		}
		m_replicationSystem->EndTick();
		m_aoiSystem->ClearTransientEvents();
		phases[4] = NOW().Ns() - phaseStartNs;

		const Timestamp profileNow = NOW();
		const uint64 profileNowNs = profileNow.Ns();
		// Preserve the metric layout; player completion no longer has a tick phase.
		phases[5] = 0;
		phases[0] = profileNowNs - tickStartNs;
		m_metrics.RecordTick(phases, m_userContexts.size(), profileNow);
		SubmitWorldMetrics(profileNow);
	}

	void ServerWorld::SubmitWorldMetrics(Timestamp now)
	{
		if (!m_metrics.IsReadyToSubmit(now))
			return;

		m_metrics.SubmitSnapshot(now);
		m_metrics.Reset();
	}

	void ServerWorld::OnUserLeft(UserId userId)
	{
		UnbindPlayer(userId);

		m_replicationSystem->OnUserLeave(userId);

		m_actorSystem->RemoveUser(userId);
	}

	void ServerWorld::BootstrapLevelActors()
	{
		if (!m_physics || m_actorLevels.instances.empty())
			return;

		for (const ActorLevelInstanceData& instance : m_actorLevels.instances)
		{
			SpawnParams params{};
			const auto result = m_actorSystem->PrepareLevelSpawn(instance, params);
			if (!result.Succeeded())
				continue;

			if (!m_physicsSystem->SpawnActor(result.entity, params.desc))
			{
				m_actorSystem->ReleaseActor(result.actorId, result.entity);
				continue;
			}

			m_aoiSystem->OnActorSpawned(result.entity);
		}
		JAM_ASSERT(ValidateActorIdentities());
	}

	std::pair<ActorId, entt::entity> ServerWorld::ExecuteSpawnActor(SpawnParams params, bool clientRequest)
	{
		const auto result = m_actorSystem->PrepareSpawn(params, clientRequest);
		if (!result.Succeeded())
			return { ActorId::Invalid(), entt::null };

		const entt::entity entity = result.entity;
		if (!m_physicsSystem->SpawnActor(entity, params.desc))
		{
			m_actorSystem->ReleaseActor(result.actorId, entity);
			return { ActorId::Invalid(), entt::null };
		}

		m_aoiSystem->OnActorSpawned(entity);
		JAM_ASSERT(ValidateActorIdentities());
		return { result.actorId, entity };
	}

	void ServerWorld::SpawnActorAsync(UserId requester, SpawnParams params, std::function<void(ActorId)> onDone)
	{
		JAM_ASSERT(IsCurrentShardContext());
		JAM_ASSERT(!IsTickInProgress());

		const ActorId actorId = [&]() -> ActorId
		{
			if (!m_alive.load(std::memory_order_acquire) || IsTickInProgress())
				return ActorId::Invalid();

			const bool clientRequest = requester != kInvalidUserId;
			if (clientRequest)
			{
				if (params.clientRequestId == kInvalidClientRequestId || !m_userContexts.contains(requester))
					return ActorId::Invalid();

				if (const auto result = m_actorSystem->FindSpawnResult(requester, params.clientRequestId))
					return *result;
			}

			params.owner = requester;
			const ActorId spawnedActorId = ExecuteSpawnActor(params, clientRequest).first;
			if (clientRequest)
			{
				m_actorSystem->RecordSpawnResult(requester, params.clientRequestId, spawnedActorId);
			}
			return spawnedActorId;
		}();

		if (onDone)
			onDone(actorId);
	}

	bool ServerWorld::ExecuteDespawnActor(UserId requester, ActorId actorId)
	{
		JAM_ASSERT(IsCurrentShardContext());
		JAM_ASSERT(!IsTickInProgress());
		if (IsTickInProgress()) return false;

		const auto result = m_actorSystem->PrepareDespawn(requester, actorId);
		if (!result.Succeeded())
			return false;

		const entt::entity targetEntity = result.entity;

		if (!m_physicsSystem->DespawnActor(targetEntity))
			return false;

		m_replicationSystem->OnActorDestroyed(targetEntity);
		m_aoiSystem->OnActorDestroyed(targetEntity);

		if (const auto* control = m_registry.try_get<ControlTag>(targetEntity))
		{
			JAM_ASSERT(GetPlayerEntity(control->userId) == targetEntity);
			UnbindPlayer(control->userId);
		}

		m_actorSystem->ReleaseActor(actorId, targetEntity);

		JAM_ASSERT(ValidateActorIdentities());
		return true;
	}

	void ServerWorld::DespawnActorAsync(UserId requester, ActorId actorId, std::function<void(bool)> onDone)
	{
		JAM_ASSERT(IsCurrentShardContext());
		JAM_ASSERT(!IsTickInProgress());
		const bool succeeded = !IsTickInProgress() && ExecuteDespawnActor(requester, actorId);
		if (onDone)
			onDone(succeeded);
	}

	bool ServerWorld::BindPlayer(UserId userId, entt::entity entity, bool alreadyBound)
	{
		if (!m_actorSystem->BindPlayer(userId, entity))
			return false;

		m_userToPlayerEntity[userId] = entity;

		if (!m_aoiSystem->PreparePlayer(userId, entity)
			|| (m_replicationSystem->IsAwaitingPlayer(userId) && !m_replicationSystem->BeginInitialSync(userId)))
		{
			if (!alreadyBound) UnbindPlayer(userId);
			return false;
		}

		if (!alreadyBound) m_replicationSystem->MarkActorDirty(entity);
		return true;
	}

	void ServerWorld::UnbindPlayer(UserId userId)
	{
		if (const auto it = m_userToPlayerEntity.find(userId); it != m_userToPlayerEntity.end())
		{
			const entt::entity player = it->second;
			m_userToPlayerEntity.erase(it);
			if (m_actorSystem->UnbindPlayer(userId, player))
			{
				m_replicationSystem->MarkActorDirty(player);
			}
		}

		m_inputSystem->ResetInputStream(userId);
		m_aoiSystem->UnregisterSubscription(userId);
		m_replicationSystem->AwaitPlayer(userId);
	}

	void ServerWorld::ProcessGameInput(UserId userId, const PacketHeaderView& pkt)
	{
		flatbuffers::Verifier verfier(pkt.Payload(), pkt.PayloadSize());
		if (!fb::VerifyfbCharacterControlCommandBuffer(verfier)) return;

		const auto* wireCommand = fb::GetfbCharacterControlCommand(pkt.Payload());
		if (!wireCommand) return;
		if (userId == 0 || !m_userContexts.contains(userId) || GetPlayerEntity(userId) == entt::null)
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

		m_inputSystem->EnqueueInput(userId, cmd);
	}

	eWorldTransitionFailure ServerWorld::JoinMembership(WorldUserContext user, WorldTransitionToken token, std::string entryPoint)
	{
		JAM_ASSERT(IsCurrentShardContext());
		JAM_ASSERT(!IsTickInProgress());

		if (IsTickInProgress()
			|| !m_alive.load(std::memory_order_acquire)
			|| !token.IsValid()
			|| user.mainRevision == 0
			|| m_membershipEntries.contains(token)
			|| m_detachedMemberships.contains(token))
		{
			return eWorldTransitionFailure::InvalidRequest;
		}

		const WorldMembershipContext context{
			.accountId			= user.accountId,
			.userId				= user.userId,
			.transitionToken	= token,
			.correlation		= { GetWorldRef(), user.mainRevision },
			.entryPoint			= std::move(entryPoint),
		};

		if (m_content && !m_content->OnJoiningMembership(*this, context))
			return eWorldTransitionFailure::InvalidRequest;

		if (!AddMember(user))
			return eWorldTransitionFailure::InternalError;

		m_membershipEntries.emplace(token, MembershipEntry{ user.userId });
		m_pendingActivations[user.userId] = context;

		if (m_content && !m_content->OnPreparedMembership(*this, context))
		{
			CancelMembership(user.userId, token);
			return eWorldTransitionFailure::InternalError;
		}

		return eWorldTransitionFailure::None;
	}

	bool ServerWorld::DetachMembership(UserId userId, WorldTransitionToken token)
	{
		JAM_ASSERT(IsCurrentShardContext());
		JAM_ASSERT(!IsTickInProgress());

		const auto member = m_userContexts.find(userId);

		if (IsTickInProgress()
			|| !m_alive.load(std::memory_order_acquire)
			|| !token.IsValid()
			|| member == m_userContexts.end()
			|| m_detachedMemberships.contains(token)
			|| m_membershipEntries.contains(token))
		{
			return false;
		}

		for (const auto& entry : m_membershipEntries | std::views::values)
			if (entry.userId == userId) return false;

		const WorldUserContext saved = member->second;
		if (m_content && !m_content->OnDetachingMembership(*this, userId, token))
			return false;

		m_detachedMemberships.emplace(token, saved);
		return RemoveMember(userId);
	}

	bool ServerWorld::CancelMembership(UserId userId, WorldTransitionToken token)
	{
		JAM_ASSERT(IsCurrentShardContext());
		JAM_ASSERT(!IsTickInProgress());

		auto entry = m_membershipEntries.find(token);

		if (IsTickInProgress() || entry == m_membershipEntries.end() || entry->second.userId != userId)
			return false;

		if (m_content && !m_content->OnRollbackMembership(*this, userId, token))
			return false;

		if (!RemoveMember(userId))
			return false;

		m_pendingActivations.erase(userId);
		m_membershipEntries.erase(token);
		return true;
	}

	bool ServerWorld::RecoverMembership(WorldUserContext user, WorldTransitionToken token)
	{
		JAM_ASSERT(IsCurrentShardContext());
		JAM_ASSERT(!IsTickInProgress());

		const auto source = m_detachedMemberships.find(token);
		if (IsTickInProgress()
			|| !m_alive.load(std::memory_order_acquire)
			|| source == m_detachedMemberships.end()
			|| source->second.userId != user.userId
			|| source->second.accountId != user.accountId || !AddMember(user))
		{
			return false;
		}


		if (m_content && !m_content->OnRestoredMembership(*this, user, token))
		{
			RemoveMember(user.userId);
			return false;
		}

		m_detachedMemberships.erase(token);
		return true;
	}

	bool ServerWorld::CompleteMembershipLeave(UserId userId, WorldTransitionToken token)
	{
		JAM_ASSERT(IsCurrentShardContext());
		JAM_ASSERT(!IsTickInProgress());

		const auto source = m_detachedMemberships.find(token);
		if (IsTickInProgress() || source == m_detachedMemberships.end() || source->second.userId != userId)
			return false;

		if (m_content && !m_content->OnRemovedMembership(*this, userId, token))
			return false;

		m_pendingActivations.erase(userId);
		m_detachedMemberships.erase(token);
		return true;
	}

	bool ServerWorld::CompleteMembershipEntry(UserId userId, WorldTransitionToken token)
	{
		JAM_ASSERT(IsCurrentShardContext());

		const auto entry = m_membershipEntries.find(token);
		if (entry == m_membershipEntries.end() || entry->second.userId != userId)
			return false;

		if (m_content)
			m_content->OnCommittedMembership(*this, userId, token);

		m_membershipEntries.erase(token);
		return true;
	}

	bool ServerWorld::ActivateMemberReplication(const WorldUserContext& user)
	{
		JAM_ASSERT(IsCurrentShardContext());
		JAM_ASSERT(!IsTickInProgress());

		const UserId userId = user.userId;
		const auto member = m_userContexts.find(userId);

		if (IsTickInProgress()
			|| !m_alive.load(std::memory_order_acquire)
			|| member == m_userContexts.end()
			|| member->second.mainRevision != user.mainRevision
			|| member->second.sessions.tcp.ownerId != user.sessions.tcp.ownerId
			|| member->second.sessions.udp.ownerId != user.sessions.udp.ownerId)
		{
			return false;
		}

		const auto pending = m_pendingActivations.find(userId);
		if (pending == m_pendingActivations.end())
		{
			return ResumeMemberReplication(userId);
		}

		WorldMembershipContext context = pending->second;
		context.correlation.mainRevision = user.mainRevision;

		const bool succeeded = !m_content || m_content->OnActivatedMembership(*this, context);
		if (succeeded)
			m_pendingActivations.erase(userId);

		return succeeded && ResumeMemberReplication(userId);
	}
}
