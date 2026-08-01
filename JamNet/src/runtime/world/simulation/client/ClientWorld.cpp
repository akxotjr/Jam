#include "pch.h"
#include "jamnet/runtime/world/simulation/client/ClientWorld.h"

#include "jamnet/core/executor/GlobalExecutor.h"
#include "jamnet/core/executor/GlobalEventBus.h"
#include "jamnet/core/executor/Lock.h"
#include "jamnet/core/executor/MainExecutor.h"
#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/core/net/RPCAPI.h"
#include "jamnet/core/net/Session.h"

#include "jamnet/runtime/world/simulation/common/ActorComponents.h"
#include "jamnet/runtime/world/simulation/common/WorldContext.h"
#include "jamnet/runtime/world/simulation/common/ShardJobBridge.h"

#include "jamnet/runtime/world/simulation/client/ClientInputSystem.h"
#include "jamnet/runtime/world/simulation/client/ClientCharacterControlCoordinator.h"
#include "jamnet/runtime/world/simulation/client/ClientReplaySystem.h"
#include "jamnet/runtime/world/simulation/client/ClientReplicationSystem.h"
#include "jamnet/runtime/world/simulation/client/ClientPhysicsSystem.h"
#include "jamnet/runtime/world/simulation/client/ClientSamplingSystem.h"

#include "jamnet/runtime/application/AppRuntimeEvents.h"

#include "jamnet/runtime/protocol/transport/CustomPacketHelper.h"
#include "jamnet/runtime/protocol/schema/gen/lifecycle_generated.h"
#include "jamnet/runtime/protocol/schema/gen/snapshot_generated.h"

#include <jampx/PhysicsFacade.h>

namespace jam::net
{

	bool ClientWorld::OnInitialize()
	{
		if (!PhysicalWorld::OnInitialize())
			return false;

		ClearActorDirectory();

		if (!m_headless)
		{
			if (auto shard = m_shard.lock())
				m_bridge = std::make_unique<ShardJobBridge>(*shard);
			else
				return false;

			m_physics->SetJobBridge(m_bridge.get());
			m_physics->Init();
		}

		InitClientPhysicalWorldCtx(m_registry);

		m_registry.ctx().emplace<ClientWorld*>(this);
		m_registry.ctx().emplace<TickCounter>().Init();
		m_registry.ctx().emplace<ClientInputSystem>(m_registry).Init();
		m_registry.ctx().emplace<ClientCharacterControlCoordinator>(*this);

		if (!m_headless)
		{
			m_registry.ctx().emplace<ClientReplicationSystem>(m_registry).Init();
			m_registry.ctx().emplace<ClientReplaySystem>(m_registry).Init();
			m_registry.ctx().emplace<ClientPhysicsSystem>(m_registry, m_physics.get()).Init();
			m_registry.ctx().emplace<ClientSamplingSystem>(m_registry).Init();

			BootstrapLevelActors();
		}

		return true;
	}

	void ClientWorld::Start(uint64 dt_ns)
	{
		JAMNET_LOG_DEBUG("[ClientWorld] domain subtype is {}", m_pipelineSubtype);
		StartTickPipeline({ DOMAIN_PHYSICS, m_pipelineSubtype }, dt_ns);
	}

	void ClientWorld::Resume(uint64 dt_ns)
	{
		Start(dt_ns);
	}

	void ClientWorld::Stop()
	{
		StopTickPipeline();
	}

	void ClientWorld::OnShutdown()
	{
		Stop();
		ShutdownPhysicsWhenPipelineStops();
	}

 
	void ClientWorld::Send(Packet packet)
	{
		if (!packet.IsValid() || !m_principal)
			return;

		const PacketHeaderView view = PacketHeaderView::Parse(packet->Head(), packet->Size());
		if (!view.IsValid())
			return;

		if (IsTcp(view.Channel()))
		{
			if (m_principal->tcp)
				m_principal->tcp->Send(packet);
			return;
		}

		if (m_principal->udp)
			m_principal->udp->Send(packet);
	}

	void ClientWorld::HandleWorldPacket(uint64 callerUserId, Packet packet)
	{
		if (m_headless)
			return;

		const PacketHeaderView view = PacketHeaderView::Parse(packet->Head(), packet->Size());

		switch (view.Id())
		{
		case CustomPacketId::LIFECYCLE:
		{
			ProcessLifecyclePacket(view);
			break;
		}
		case CustomPacketId::SNAPSHOT:
		{
			ProcessSnapshot(view);
			break;
		}

		default: break;
		}
	}

	bool ClientWorld::AddMember(WorldUserContext user)
	{
		const uint64 participantUserId = user.userId;
		const bool added = WorldMembershipHost::AddMember(user);
		if (added)
			PublishWorldParticipantEvent(participantUserId, eWorldParticipantChange::Joined);
		return added;
	}

	bool ClientWorld::RemoveMember(uint64 userId)
	{
		const bool removed = WorldMembershipHost::RemoveMember(userId);
		if (removed)
			PublishWorldParticipantEvent(userId, eWorldParticipantChange::Left);
		return removed;
	}

	void ClientWorld::SubmitActorAction(const ActorActionCommand& command, const WorldEventCorrelation& correlation)
	{
		JAM_ASSERT(IsCurrentShardContext());

		std::visit([this, &correlation]<typename ReqT>(const ReqT& request)
			{
				constexpr eActorAction action = std::is_same_v<ReqT, SpawnActorRequest> ? eActorAction::Spawn : eActorAction::Despawn;

				if (request.requestId == kInvalidClientRequestId)
				{
					PublishActorActionResult(request.requestId, ActorActionResult{ .reason = eActorActionReason::InvalidArgument, .action = action });
					return;
				}

				if constexpr (std::is_same_v<ReqT, SpawnActorRequest>)
				{
					SpawnParams params{};
					if (!BuildSpawnParams(request.spec, request.requestId, params))
					{
						PublishActorActionResult(request.requestId, ActorActionResult{ .reason = eActorActionReason::InvalidArgument, .action = action });
						return;
					}
					if (!correlation.world.IsValid() || correlation.world != GetWorldRuntime() || correlation.mainRevision == 0)
					{
						PublishActorActionResult(request.requestId, ActorActionResult{ .reason = eActorActionReason::WorldUnavailable, .action = action });
						return;
					}
					params.correlation = correlation;

					if (params.controlled)
						RequestSpawnPlayer(params);
					else
						RequestSpawnActor(params);
				}
				else
				{
					if (!request.actorId.IsValid())
					{
						PublishActorActionResult(request.requestId, ActorActionResult{ .reason = eActorActionReason::InvalidArgument, .action = action });
						return;
					}

					const entt::entity entity = ResolveActor(request.actorId);
					if (entity == entt::null || !m_registry.valid(entity) || !m_registry.all_of<ActorId>(entity))
					{
						PublishActorActionResult(request.requestId, ActorActionResult{ .reason = eActorActionReason::ActorNotFound, .action = action });
						return;
					}

					const auto* control = m_registry.try_get<ControlTag>(entity);
					if (control && control->userId == GetUserId())
						RequestDespawnPlayer(request);
					else
						RequestDespawnActor(request);
				}
			}, command.payload);
	}

	bool ClientWorld::BuildSpawnParams(const FrontendSpawnActorSpec& spec, ClientRequestId requestId, OUT SpawnParams& outParams) const
	{
		if (!m_physics || !IsValidAssetKey(spec.actorArchetypeKey))
			return false;
		const ActorArchetypeData* actorArchetype = m_actorArchetypes.Find(spec.actorArchetypeKey);
		if (!actorArchetype || !actorArchetype->AllowsSpawn(eActorSpawnSource::Runtime))
			return false;

		px::PhysicsArchetypeKey physicsArchetype{};
		if (!TryResolvePhysicsArchetypeKey(spec.actorArchetypeKey, physicsArchetype))
			return false;

		const px::eBodyType bodyType = m_physics->FindBodyType(physicsArchetype);
		if (bodyType == px::eBodyType::None)
			return false;

		outParams = {};
		outParams.actorArchetypeKey = spec.actorArchetypeKey;
		outParams.clientRequestId = requestId;
		outParams.owned = spec.requestOwnership;
		outParams.controlled = spec.requestControl;
		outParams.targetActorId = spec.targetActorId;
		outParams.desc.archetype = physicsArchetype;
		outParams.desc.pose = spec.pose;
		outParams.desc.spawnSrc = px::eSpawnSource::Runtime;
		outParams.desc.team = spec.team;
		outParams.desc.part = spec.part;
		outParams.desc.role = spec.role;

		if (bodyType == px::eBodyType::Rigid)
		{
			px::RigidSpawnOverrides overrides{};
			if (spec.linearVelocity)  { overrides.mask.set(px::SpawnOverrideMask::LINEAR_VEL);   overrides.linearVelocity  = *spec.linearVelocity; }
			if (spec.angularVelocity) { overrides.mask.set(px::SpawnOverrideMask::ANGULAR_VEL);  overrides.angularVelocity = *spec.angularVelocity; }
			if (spec.linearDamping)   { overrides.mask.set(px::SpawnOverrideMask::LINEAR_DAMP);  overrides.linearDamping   = *spec.linearDamping; }
			if (spec.angularDamping)  { overrides.mask.set(px::SpawnOverrideMask::ANGULAR_DAMP); overrides.angularDamping  = *spec.angularDamping; }
			outParams.desc.overrides = overrides;
		}
		else
		{
			px::CharacterSpawnOverrides overrides{};
			if (spec.viewYaw)   { overrides.mask.set(px::SpawnOverrideMask::VIEW_YAW);   overrides.yaw   = *spec.viewYaw; }
			if (spec.viewPitch) { overrides.mask.set(px::SpawnOverrideMask::VIEW_PITCH); overrides.pitch = *spec.viewPitch; }
			outParams.desc.overrides = overrides;
		}

		return true;
	}

	void ClientWorld::SubmitCharacterControl(CharacterControlIntent intent)
	{
		JAM_ASSERT(IsCurrentShardContext());
		if (auto* coordinator = m_registry.ctx().find<ClientCharacterControlCoordinator>())
			coordinator->Submit(std::move(intent));
	}

	void ClientWorld::RequestHitscan(const px::Vec3& from, const px::Vec3& dir, float maxRange, std::function<void(const px::HitscanResult&)> onDone)
	{
		JAM_ASSERT(IsCurrentShardContext());

		px::HitscanResult result{};
		if (m_physics)
			result = m_physics->Hitscan(from, dir, maxRange, 0);

		if (onDone)
		{
			MAIN_EXEC.Submit(Job([cb = std::move(onDone), result]()
				{
					cb(result);
				}));
		}
	}


	void ClientWorld::RequestSpawnPlayer(const SpawnParams& params)
	{
		if (!IsValidAssetKey(params.desc.archetype))
			return;

		flatbuffers::FlatBufferBuilder fbb(256);
		const fb::fbVec3 pos(params.desc.pose.p.x, params.desc.pose.p.y, params.desc.pose.p.z);
		const fb::fbQuat rot(params.desc.pose.q.x, params.desc.pose.q.y, params.desc.pose.q.z, params.desc.pose.q.w);
		fb::fbVec3 linearVel{};
		fb::fbVec3 angularVel{};
		const fb::fbVec3* linearVelPtr = nullptr;
		const fb::fbVec3* angularVelPtr = nullptr;
		uint32 overrideMask = 0;
		float linearDamping = 0.0f;
		float angularDamping = 0.0f;
		float yaw = 0.0f;
		float pitch = 0.0f;

		if (params.desc.IsRigid())
		{
			const auto& overrides = std::get<px::RigidSpawnOverrides>(params.desc.overrides);
			overrideMask = overrides.mask.bits();

			if (overrides.mask.has_any(px::SpawnOverrideMask::LINEAR_VEL))
			{
				linearVel = fb::fbVec3(overrides.linearVelocity.x, overrides.linearVelocity.y, overrides.linearVelocity.z);
				linearVelPtr = &linearVel;
			}
			if (overrides.mask.has_any(px::SpawnOverrideMask::ANGULAR_VEL))
			{
				angularVel = fb::fbVec3(overrides.angularVelocity.x, overrides.angularVelocity.y, overrides.angularVelocity.z);
				angularVelPtr = &angularVel;
			}
			if (overrides.mask.has_any(px::SpawnOverrideMask::LINEAR_DAMP))
				linearDamping = overrides.linearDamping;
			if (overrides.mask.has_any(px::SpawnOverrideMask::ANGULAR_DAMP))
				angularDamping = overrides.angularDamping;
		}
		else
		{
			const auto& overrides = std::get<px::CharacterSpawnOverrides>(params.desc.overrides);
			overrideMask = overrides.mask.bits();

			if (overrides.mask.has_any(px::SpawnOverrideMask::VIEW_YAW))
				yaw = overrides.yaw;
			if (overrides.mask.has_any(px::SpawnOverrideMask::VIEW_PITCH))
				pitch = overrides.pitch;
		}

		const auto root = fb::CreatefbSpawnPlayerReq(
			fbb,
			params.correlation.world.worldId,
			params.correlation.world.instance.instanceId.value,
			params.correlation.mainRevision,
			params.clientRequestId,
			params.actorArchetypeKey.v,
			&pos,
			&rot,
			static_cast<uint32>(params.desc.spawnSrc),
			params.desc.team,
			params.desc.part,
			params.desc.role,
			overrideMask,
			linearVelPtr,
			angularVelPtr,
			linearDamping,
			angularDamping,
			yaw,
			pitch,
			params.targetActorId.Value());
		fbb.Finish(root);

		JAMNET_LOG_DEBUG("[ClientPhysicalWorld::RequestSpawnPlayer] account id= {}, user id= {}", GetAccountId(), GetUserId());

		RPCCallOptions opt{ .channel = eChannel::TCP_DEFAULT, .timeout_ns = 15_s };
		RPCCallAsync<fb::fbSpawnPlayerReq, fb::fbSpawnPlayerRes>(
			m_principal->tcp,
			fbb.GetBufferPointer(),
			static_cast<uint32>(fbb.GetSize()),
			opt,
			[this, requestId = params.clientRequestId](std::optional<RPCTableRef<fb::fbSpawnPlayerRes>> res)
			{
				OnSpawnPlayerResponse(requestId, std::move(res));
			});
	}

	void ClientWorld::RequestSpawnActor(const SpawnParams& params)
	{
		if (!IsValidAssetKey(params.desc.archetype))
			return;

		flatbuffers::FlatBufferBuilder fbb(256);
		const fb::fbVec3 pos(params.desc.pose.p.x, params.desc.pose.p.y, params.desc.pose.p.z);
		const fb::fbQuat rot(params.desc.pose.q.x, params.desc.pose.q.y, params.desc.pose.q.z, params.desc.pose.q.w);
		fb::fbVec3 linearVel{};
		fb::fbVec3 angularVel{};
		const fb::fbVec3* linearVelPtr  = nullptr;
		const fb::fbVec3* angularVelPtr = nullptr;
		uint32 overrideMask = 0;
		float linearDamping = 0.0f;
		float angularDamping = 0.0f;
		float yaw = 0.0f;
		float pitch = 0.0f;

		if (params.desc.IsRigid())
		{
			const auto& overrides = std::get<px::RigidSpawnOverrides>(params.desc.overrides);
			overrideMask = overrides.mask.bits();
			if (overrides.mask.has_any(px::SpawnOverrideMask::LINEAR_VEL))
			{
				linearVel = fb::fbVec3(overrides.linearVelocity.x, overrides.linearVelocity.y, overrides.linearVelocity.z);
				linearVelPtr = &linearVel;
			}
			if (overrides.mask.has_any(px::SpawnOverrideMask::ANGULAR_VEL))
			{
				angularVel = fb::fbVec3(overrides.angularVelocity.x, overrides.angularVelocity.y, overrides.angularVelocity.z);
				angularVelPtr = &angularVel;
			}
			if (overrides.mask.has_any(px::SpawnOverrideMask::LINEAR_DAMP)) linearDamping = overrides.linearDamping;
			if (overrides.mask.has_any(px::SpawnOverrideMask::ANGULAR_DAMP)) angularDamping = overrides.angularDamping;
		}
		else
		{
			const auto& overrides = std::get<px::CharacterSpawnOverrides>(params.desc.overrides);
			overrideMask = overrides.mask.bits();
			if (overrides.mask.has_any(px::SpawnOverrideMask::VIEW_YAW)) yaw = overrides.yaw;
			if (overrides.mask.has_any(px::SpawnOverrideMask::VIEW_PITCH)) pitch = overrides.pitch;
		}

		const auto root = fb::CreatefbSpawnActorReq(
			fbb, GetWorldId(), GetWorldInstance().instanceId.value, params.clientRequestId,
			params.owned ? GetUserId() : 0, 0, params.actorArchetypeKey.v, &pos, &rot,
			static_cast<uint32>(params.desc.spawnSrc), params.desc.team, params.desc.part, params.desc.role,
			overrideMask, linearVelPtr, angularVelPtr, linearDamping, angularDamping, yaw, pitch, params.targetActorId.Value());
		fbb.Finish(root);

		RPCCallAsync<fb::fbSpawnActorReq, fb::fbSpawnActorRes>(
			m_principal->udp, 
			fbb.GetBufferPointer(), 
			static_cast<uint32>(fbb.GetSize()),
			{ .channel = eChannel::RELIABLE_ORDERED, .timeout_ns = 1_s },
			[this, requestId = params.clientRequestId](std::optional<RPCTableRef<fb::fbSpawnActorRes>> res)
			{
				OnSpawnActorResponse(requestId, std::move(res));
			});
	}

	void ClientWorld::RequestDespawnActor(const DespawnActorRequest& request)
	{
		flatbuffers::FlatBufferBuilder fbb(64);
		const auto root = fb::CreatefbDespawnActorReq(fbb, GetWorldId(), GetWorldInstance().instanceId.value, request.actorId.Value());
		fbb.Finish(root);

		RPCCallOptions opt{ .channel = eChannel::RELIABLE_ORDERED, .timeout_ns = 1_s };
		RPCCallAsync<fb::fbDespawnActorReq, fb::fbDespawnActorRes>(
			m_principal->udp,
			fbb.GetBufferPointer(),
			static_cast<uint32>(fbb.GetSize()),
			opt,
			[this, requestId = request.requestId, actorId = request.actorId](std::optional<RPCTableRef<fb::fbDespawnActorRes>> res)
			{
				OnDespawnActorResponse(requestId, actorId, std::move(res));
			});
	}

	void ClientWorld::RequestDespawnPlayer(const DespawnActorRequest& request)
	{
		const auto user = m_userContexts.find(GetUserId());
		if (user == m_userContexts.end() || user->second.mainRevision == 0)
			return;

		flatbuffers::FlatBufferBuilder fbb(64);
		const auto root = fb::CreatefbDespawnPlayerReq(fbb, GetWorldId(), GetWorldInstance().instanceId.value, user->second.mainRevision, request.actorId.Value());
		fbb.Finish(root);

		RPCCallAsync<fb::fbDespawnPlayerReq, fb::fbDespawnPlayerRes>(
			m_principal->tcp, 
			fbb.GetBufferPointer(), 
			static_cast<uint32>(fbb.GetSize()),
			{ .channel = eChannel::TCP_DEFAULT, .timeout_ns = 10_s },
			[this, requestId = request.requestId, actorId = request.actorId](std::optional<RPCTableRef<fb::fbDespawnPlayerRes>> res)
			{
				OnDespawnPlayerResponse(requestId, actorId, std::move(res));
			});
	}

	void ClientWorld::SetReplicatedActorDormant(ActorId actorId)
	{
		SetActorDormantImpl(actorId);
	}

	void ClientWorld::HideReplicatedActorUntilConfirmed(ActorId actorId)
	{
		const auto e = ResolveActor(actorId);
		if (e == entt::null || !m_registry.valid(e) || m_registry.all_of<LocallyHiddenTag>(e))
			return;

		const bool wasActive = !m_registry.all_of<OutOfAoiTag>(e);

		if (m_registry.all_of<PhysicsSpawnedTag>(e))
		{
			if (m_registry.ctx().contains<ClientPhysicsSystem>())
				m_registry.ctx().get<ClientPhysicsSystem>().DespawnActor(e);
			else
				m_registry.erase<PhysicsSpawnedTag>(e);
		}

		m_registry.remove<ReplayRelevantTag>(e);
		m_registry.emplace_or_replace<LocallyHiddenTag>(e);

		if (wasActive)
			PublishActorDespawned(e, eActorLifecycleReason::LocallyHidden);
	}

	void ClientWorld::ReactivateReplicatedActor(ActorId actorId, bool isLocal)
	{
		const auto e = ResolveActor(actorId);
		if (e == entt::null || !m_registry.valid(e))
			return;

		const bool hiddenByAoi = m_registry.all_of<OutOfAoiTag>(e);
		const bool hiddenLocally = m_registry.all_of<LocallyHiddenTag>(e);
		if (!hiddenByAoi && !hiddenLocally)
			return;

		m_registry.remove<OutOfAoiTag>(e);
		m_registry.remove<LocallyHiddenTag>(e);
		PublishActorSpawned(e, 0, isLocal, eActorLifecycleReason::AoiEntered);
	}

	void ClientWorld::DestroyReplicatedActor(ActorId actorId)
	{
		DespawnActorImpl(actorId);
	}

bool ClientWorld::TryResolvePhysicsArchetypeKey(ActorArchetypeKey actorArchetypeKey, OUT px::PhysicsArchetypeKey& outKey) const
{
	const ActorArchetypeData* actorArchetype = m_actorArchetypes.Find(actorArchetypeKey);
	if (!actorArchetype || !IsValidAssetKey(actorArchetype->physicsArchetype))
		return false;

	outKey = actorArchetype->physicsArchetype;
	return true;
}

entt::entity ClientWorld::EnsureReplicatedActor(ActorId actorId, ActorArchetypeKey actorArchetypeKey, uint64 owner, uint64 controller, px::eBodyType bodyType, bool* outCreated)
{
	if (!actorId.IsValid())
	{
		if (outCreated) *outCreated = false;
		return entt::null;
	}

	if (const entt::entity existing = ResolveActor(actorId); existing != entt::null)
	{
		if (outCreated) *outCreated = false;
		return existing;
	}

	px::PhysicsArchetypeKey physicsArchetypeKey{};
	if (!TryResolvePhysicsArchetypeKey(actorArchetypeKey, physicsArchetypeKey))
	{
		if (outCreated) *outCreated = false;
		return entt::null;
	}

	entt::entity e = m_registry.create();
	if (!m_actorDirectory.Bind(actorId, e))
	{
		m_registry.destroy(e);
		if (outCreated) *outCreated = false;
		return entt::null;
	}

	m_registry.emplace<ActorId>(e, actorId);
	m_registry.emplace<ActorArchetypeRef>(e, ActorArchetypeRef{ actorArchetypeKey });
	m_registry.emplace<PhysicsArchetypeRef>(e, PhysicsArchetypeRef{ physicsArchetypeKey });
	m_registry.emplace<ActorTeamPartRole>(e);
	m_registry.emplace<OwnershipTag>(e, OwnershipTag{ owner });
	m_registry.emplace<ControlTag>(e, ControlTag{ controller });
	m_registry.emplace<ActorBodyType>(e, ActorBodyType{ bodyType });
	if (bodyType == px::eBodyType::Rigid)
	{
		m_registry.emplace<RigidAuthorityState>(e);
		m_registry.emplace<RigidProxyState>(e);
		m_registry.emplace<RigidReplayHistory>(e);
	}
	else
	{
		m_registry.emplace<CharAuthorityState>(e);
		m_registry.emplace<CharProxyState>(e);
		m_registry.emplace<CharReplayHistory>(e);
	}

	if (m_physics && m_physics->IsReplayCandidate(physicsArchetypeKey))
	{
		m_registry.emplace<ReplayCandidateTag>(e);
		m_registry.emplace<ReplayRetention>(e, ReplayRetention{});
	}

	if (outCreated) *outCreated = true;
	JAM_ASSERT(ValidateActorIdentities());
	return e;
}

	void ClientWorld::Tick()
	{
		m_registry.ctx().get<TickCounter>().Tick();
		m_registry.ctx().get<ClientInputSystem>().Tick();

		if (m_headless) return;

		m_registry.ctx().get<ClientReplicationSystem>().Tick();
		m_registry.ctx().get<ClientReplaySystem>().Tick();
		m_registry.ctx().get<ClientPhysicsSystem>().Tick();
		m_registry.ctx().get<ClientSamplingSystem>().Tick();
	}

	void ClientWorld::ProcessLifecyclePacket(const PacketHeaderView& view)
	{
		flatbuffers::Verifier verifier(view.Payload(), view.PayloadSize());
		if (!fb::VerifyfbLifecycleBatchBuffer(verifier))
			return;

		const auto fbBatch = fb::UnPackfbLifecycleBatch(view.Payload());
		if (!fbBatch) return;

		const auto batch = std::make_shared<fb::fbLifecycleBatchT>(std::move(*fbBatch));
		
		if (auto* repl = m_registry.ctx().find<ClientReplicationSystem>())
			repl->EnqueueLifecycle(std::move(*batch));
	}

	void ClientWorld::ProcessSnapshot(const PacketHeaderView& view)
	{
		flatbuffers::Verifier verifier(view.Payload(), view.PayloadSize());
		if (!fb::VerifyfbSnapshotBuffer(verifier))
			return;

		const uint64 recvNs = NOW_NS();

		const auto fbSnap = fb::UnPackfbSnapshot(view.Payload());
		if (!fbSnap) return;

		const auto snap = std::make_shared<fb::fbSnapshotT>(std::move(*fbSnap));

		if (auto* repl = m_registry.ctx().find<ClientReplicationSystem>())
		{
			repl->EnqueueSnapshot(std::move(*snap), recvNs);
		}

	}


	void ClientWorld::OnSpawnActorResponse(ClientRequestId requestId, std::optional<RPCTableRef<fb::fbSpawnActorRes>> res)
	{
		if (!res)
		{
			JAMNET_LOG_WARN_LOC("Spawn RPC timeout or connection lost\n");
			PublishActorActionResult(requestId, ActorActionResult{ .reason = eActorActionReason::TransportUnavailable, .action = eActorAction::Spawn });
			return;
		}

		ActorId actorId((*res)->actor_id());
		if (const entt::entity entity = ResolveActor(actorId); entity != entt::null && m_registry.valid(entity))
		{
			actorId = GetActorId(entity);
		}

		PublishActorActionResult(requestId, ActorActionResult
			{
				.status = (*res)->success() ? eActorActionStatus::Succeeded : eActorActionStatus::Failed,
				.reason = (*res)->success()
					? eActorActionReason::None
					: (((*res)->failure() == fb::fbSpawnActorFailure_InvalidCorrelation
						|| (*res)->failure() == fb::fbSpawnActorFailure_StaleRevision)
						? eActorActionReason::WorldUnavailable
						: eActorActionReason::Rejected),
				.action  = eActorAction::Spawn,
				.actorId = actorId,
			});

	}

	void ClientWorld::OnDespawnActorResponse(ClientRequestId requestId, ActorId actorId, std::optional<RPCTableRef<fb::fbDespawnActorRes>> res)
	{
		if (!res.has_value())
		{
			JAMNET_LOG_WARN_LOC("Despawn RPC timeout or connection lost\n");
			PublishActorActionResult(requestId, ActorActionResult{ .reason = eActorActionReason::TransportUnavailable, .action = eActorAction::Despawn, .actorId = actorId });
			return;
		}

		if (!(*res)->success())
		{
			JAMNET_LOG_WARN_LOC("Despawn RPC failed on server\n");
		}

		PublishActorActionResult(requestId, ActorActionResult
			{
				.status = (*res)->success() ? eActorActionStatus::Succeeded : eActorActionStatus::Failed,
				.reason = (*res)->success() ? eActorActionReason::None : eActorActionReason::Rejected,
				.action = eActorAction::Despawn,
				.actorId = actorId,
			});
	}

	void ClientWorld::OnSpawnPlayerResponse(ClientRequestId requestId, std::optional<RPCTableRef<fb::fbSpawnPlayerRes>> res)
	{
		if (!res)
		{
			PublishActorActionResult(requestId, ActorActionResult{ .reason = eActorActionReason::TransportUnavailable, .action = eActorAction::Spawn });
			return;
		}

		ActorId actorId((*res)->actor_id());
		if (const entt::entity entity = ResolveActor(actorId); entity != entt::null && m_registry.valid(entity))
			actorId = GetActorId(entity);

		const auto failure = (*res)->failure();
		PublishActorActionResult(requestId, ActorActionResult
			{
				.status = (*res)->success() ? eActorActionStatus::Succeeded : eActorActionStatus::Failed,
				.reason = (*res)->success() ? eActorActionReason::None
					: ((failure == fb::fbSpawnPlayerFailure_InvalidCorrelation || failure == fb::fbSpawnPlayerFailure_StaleRevision)
						? eActorActionReason::WorldUnavailable : eActorActionReason::Rejected),
				.action = eActorAction::Spawn,
				.actorId = actorId,
			});
	}

	void ClientWorld::OnDespawnPlayerResponse(ClientRequestId requestId, ActorId actorId, std::optional<RPCTableRef<fb::fbDespawnPlayerRes>> res)
	{
		if (!res)
		{
			PublishActorActionResult(requestId, ActorActionResult{ .reason = eActorActionReason::TransportUnavailable, .action = eActorAction::Despawn, .actorId = actorId });
			return;
		}

		PublishActorActionResult(requestId, ActorActionResult
			{
				.status = (*res)->success() ? eActorActionStatus::Succeeded : eActorActionStatus::Failed,
				.reason = (*res)->success() ? eActorActionReason::None : eActorActionReason::Rejected,
				.action = eActorAction::Despawn,
				.actorId = actorId,
			});
	}

	void ClientWorld::PublishActorActionResult(ClientRequestId requestId, ActorActionResult result) const
	{
		if (requestId == kInvalidClientRequestId)
			return;

		ActorActionResultEvent event
		{
			.accountId	= GetAccountId(),
			.userId		= GetUserId(),
			.requestId	= requestId,
			.result		= result,
		};

		GLOBAL_EVENTBUS_PUBLISH(event);
	}

	void ClientWorld::PublishActorSpawned(entt::entity e, ClientRequestId requestId, bool isLocal, eActorLifecycleReason reason)
	{
		if (!m_registry.valid(e) || !m_registry.all_of<ActorId>(e))
			return;

		ActorLifecycleEvent event{};
		event.accountId		= GetAccountId();
		event.userId		= GetUserId();
		event.worldId		= m_config.RuntimeRef().worldId;
		event.clientRequestId = requestId;
		event.actorId		= GetActorId(e);
		event.isLocal		= isLocal;
		event.reason		= reason;
		if (const auto* actorArchetype = m_registry.try_get<ActorArchetypeRef>(e))
			event.actorArchetypeKey = actorArchetype->key;

		GLOBAL_EVENTBUS_PUBLISH(event);
	}

	void ClientWorld::PublishActorDespawned(entt::entity e, eActorLifecycleReason reason)
	{
		if (!m_registry.valid(e) || !m_registry.all_of<ActorId>(e))
			return;

		ActorLifecycleEvent event{};
		event.accountId	   = GetAccountId();
		event.userId       = GetUserId();
		event.worldId      = m_config.RuntimeRef().worldId;
		event.actorId	   = GetActorId(e);
		event.reason	   = reason;
		GLOBAL_EVENTBUS_PUBLISH(event);
	}

	void ClientWorld::PublishWorldParticipantEvent(uint64 participantUserId, eWorldParticipantChange change)
	{
		if (participantUserId == 0 || !GetWorldRuntime().IsValid())
			return;

		WorldParticipantEvent event{};
		event.accountId = GetAccountId();
		event.userId	= GetUserId();
		event.change	= change;
		event.participant = WorldParticipantView
		{
			.runtime = GetWorldRuntime(),
			.participantUserId = participantUserId,
		};
		GLOBAL_EVENTBUS_PUBLISH(event);
	}

	void ClientWorld::BootstrapLevelActors()
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
				JAMNET_LOG_WARN("[ClientPhysicalWorld::BootstrapLevelActors] invalid or duplicate actorId={}", instance.actorId);
				continue;
			}

			const entt::entity e = m_registry.create();
			if (!m_actorDirectory.Bind(actorId, e))
			{
				m_registry.destroy(e);
				continue;
			}

			m_registry.emplace<ActorId>(e, actorId);
			m_registry.emplace<ActorArchetypeRef>(e, ActorArchetypeRef{ actorArchetype->key });
			m_registry.emplace<PhysicsArchetypeRef>(e, PhysicsArchetypeRef{ physicsArchetypeKey });
			m_registry.emplace<OwnershipTag>(e);
			m_registry.emplace<ControlTag>(e);
			m_registry.emplace<ActorTeamPartRole>(e);
			m_registry.emplace<ActorBodyType>(e, ActorBodyType{ bodyType });
			if (m_physics->FindMotionType(physicsArchetypeKey) == px::eMotionType::Static)
				m_registry.emplace<ReplicationStaticTag>(e);

			if (bodyType == px::eBodyType::Character)
			{
				px::CharacterState state{};
				state.pos = instance.pose.p;
				m_registry.emplace<CharAuthorityState>(e, state);
				m_registry.emplace<CharProxyState>(e, state);
				m_registry.emplace<CharReplayHistory>(e);
			}
			else
			{
				px::RigidState state{};
				state.pose = instance.pose;
				m_registry.emplace<RigidAuthorityState>(e, state);
				m_registry.emplace<RigidProxyState>(e, state);
				m_registry.emplace<RigidReplayHistory>(e);
			}

			if (m_physics->IsReplayCandidate(physicsArchetypeKey))
			{
				m_registry.emplace<ReplayCandidateTag>(e);
				m_registry.emplace<ReplayRetention>(e, ReplayRetention{});
			}

			m_registry.ctx().get<ClientPhysicsSystem>().SpawnActor(e, false);
			if (!m_registry.valid(e))
			{
				m_actorDirectory.Unbind(actorId);
				continue;
			}

			if (!m_registry.all_of<PhysicsSpawnedTag>(e) && !m_physics->IsStepPending())
			{
				ReleaseActorId(e);
				m_registry.destroy(e);
				continue;
			}

			// Authored level actors already own their presentation object through
			// the world prefab. Keep their native ECS/physics bootstrap local and
			// reserve lifecycle events for runtime-created actors.
		}
		JAM_ASSERT(ValidateActorIdentities());
	}

	void ClientWorld::SetActorDormantImpl(ActorId actorId)
	{
		if (actorId == m_localActorId)
			return;

		const auto e = ResolveActor(actorId);
		if (e == entt::null || !m_registry.valid(e) || m_registry.all_of<OutOfAoiTag>(e) || m_registry.all_of<LocallyHiddenTag>(e))
			return;

		if (m_registry.all_of<PhysicsSpawnedTag>(e))
		{
			if (m_registry.ctx().contains<ClientPhysicsSystem>())
				m_registry.ctx().get<ClientPhysicsSystem>().DespawnActor(e);
			else
				m_registry.erase<PhysicsSpawnedTag>(e);
		}

		m_registry.remove<ReplayRelevantTag>(e);
		m_registry.emplace_or_replace<OutOfAoiTag>(e);
		PublishActorDespawned(e, eActorLifecycleReason::AoiLeft);
	}

	void ClientWorld::DespawnActorImpl(const ActorId actorId)
	{
		const auto e = ResolveActor(actorId);

		if (e == entt::null || !m_registry.valid(e))
			return;

		const bool wasActive = !m_registry.all_of<OutOfAoiTag>(e) && !m_registry.all_of<LocallyHiddenTag>(e);

		if (m_registry.all_of<PhysicsSpawnedTag>(e))
		{
			if (m_registry.ctx().contains<ClientPhysicsSystem>())
				m_registry.ctx().get<ClientPhysicsSystem>().DespawnActor(e);
			else
				m_registry.erase<PhysicsSpawnedTag>(e);
		}

		if (actorId == m_localActorId)
			m_localActorId = ActorId::Invalid();

		if (wasActive)
			PublishActorDespawned(e, eActorLifecycleReason::Despawned);

		ReleaseActorId(e);
		m_registry.destroy(e);
		JAM_ASSERT(ValidateActorIdentities());
	}

	void ClientWorld::RollbackPhysicsSpawn(entt::entity entity)
	{
		if (!m_registry.valid(entity))
			return;

		const auto* actorId = m_registry.try_get<ActorId>(entity);
		if (actorId && *actorId == m_localActorId)
			m_localActorId = ActorId::Invalid();

		ReleaseActorId(entity);
		m_registry.destroy(entity);
		JAM_ASSERT(ValidateActorIdentities());
	}

}
