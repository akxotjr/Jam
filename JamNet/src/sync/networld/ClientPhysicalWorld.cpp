#include "pch.h"
#include "jamnet/sync/networld/ClientPhysicalWorld.h"

#include "jamnet/core/executor/GlobalExecutor.h"
#include "jamnet/core/executor/GlobalEventBus.h"
#include "jamnet/core/executor/Lock.h"
#include "jamnet/core/executor/MainExecutor.h"
#include "jamnet/core/executor/ShardInvoke.h"
#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/core/net/RPCAPI.h"
#include "jamnet/core/net/Session.h"

#include "jamnet/sync/transport/CustomPacketHelper.h"
#include "jamnet/sync/replication/NetActorComponents.h"
#include "jamnet/sync/replication/WorldContext.h"
#include "jamnet/sync/replication/ClientInputSystem.h"
#include "jamnet/sync/replication/ClientReplaySystem.h"
#include "jamnet/sync/replication/ClientReplicationSystem.h"
#include "jamnet/sync/replication/ClientPhysicsSystem.h"
#include "jamnet/sync/replication/ClientSamplingSystem.h"
#include "jamnet/runtime/AppRuntimeEvents.h"

#include "jamnet/sync/schema/gen/lifecycle_generated.h"
#include "jamnet/sync/schema/gen/snapshot_generated.h"

namespace jam::net
{
	namespace
	{
		inline const RouteDomain kClientWorldRouteDomain = RouteDomain::From("ClientWorld");

	

		uint16 NextClientWorldLocalIndex()
		{
			static std::atomic<uint32> g_nextClientWorldLocalIndex = 1;
			const uint32 next = g_nextClientWorldLocalIndex.fetch_add(1, std::memory_order_relaxed);
			return static_cast<uint16>(next == 0 ? 1 : next);
		}
	}

	bool ClientPhysicalWorld::Init()
	{
		if (!BootstrapLocalRoute())
			return false;

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

		m_registry.ctx().emplace<ClientPhysicalWorld*>(this);
		m_registry.ctx().emplace<TickCounter>().Init();
		m_registry.ctx().emplace<ClientInputSystem>(m_registry).Init();

		if (!m_headless)
		{
			m_registry.ctx().emplace<ClientReplicationSystem>(m_registry).Init();
			m_registry.ctx().emplace<ClientReplaySystem>(m_registry).Init();
			m_registry.ctx().emplace<ClientPhysicsSystem>(m_registry, m_physics.get()).Init();
			m_registry.ctx().emplace<ClientSamplingSystem>(m_registry).Init();

			BootstrapLevelActors();
		}

		return RegisterWorldRouting();
	}

	void ClientPhysicalWorld::Start(uint64 dt_ns)
	{
		auto& L = CurrentShardLocalChecked();
		RegisterShardSystemFn(L, { DOMAIN_PHYSICS, m_localWorldIndex }, dt_ns, [this](ShardLocal&, uint64, uint64) { Tick(); });
	}

	void ClientPhysicalWorld::Resume(uint64 dt_ns)
	{
		Start(dt_ns);
	}

	void ClientPhysicalWorld::Stop()
	{
		auto& L = CurrentShardLocalChecked();
		L.domainGroups.erase({ DOMAIN_PHYSICS, m_localWorldIndex });
	}

	void ClientPhysicalWorld::Shutdown(eMailboxCloseMode mode, std::function<void()> onClosed)
	{
		m_alive.store(false, std::memory_order_relaxed);

		auto close = [this, onClosed = std::move(onClosed)]() mutable
			{
				m_shard.reset();
				if (m_localShardIndex != std::numeric_limits<uint16>::max())
					GLOBAL_EXEC.ReleaseRoute(m_localShardIndex);
				m_localShardIndex = std::numeric_limits<uint16>::max();
				m_mailboxRef = {};

				FinalizeShutdown();

				if (onClosed)
					onClosed();
			};

		auto shard = m_shard.lock();
		if (!shard || !m_mailboxRef.mailbox)
		{
			close();
			return;
		}

		shard->CloseMailbox(m_mailboxRef.mailbox->GetId(), mode, [close = std::move(close)]() mutable { close(); });
	}

	void ClientPhysicalWorld::SetSessionBundle(const ClientSessionBundle& sessions)
	{
		m_sessions = sessions;
	}

	void ClientPhysicalWorld::SetPhysicsFacade(std::unique_ptr<px::IPhysicsFacade> physics)
	{
		m_physics = std::move(physics);
	}

 
	entt::entity ClientPhysicalWorld::GetEntity(NetId netId)
	{
		if (auto it = m_netIdToEntity.find(netId); it != m_netIdToEntity.end())
		{
			return it->second;
		}
		return entt::null;
	}


	void ClientPhysicalWorld::Send(Packet packet)
	{
		if (!packet.IsValid())
			return;

		const PacketHeaderView view = PacketHeaderView::Parse(packet->Head(), packet->Size());
		if (!view.IsValid())
			return;

		if (IsTcp(view.Channel()))
		{
			if (auto* tcp = m_sessions.TryGetTcp())
				tcp->Send(packet);
			return;
		}

		if (auto* udp = m_sessions.TryGetUdp())
			udp->Send(packet);
	}

	void ClientPhysicalWorld::HandleWorldPacket(uint64 callerUserId, Packet packet)
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
			//JAMNET_LOG_DEBUG("HandleWorldPacket: receive SNAPSHOT packet. size= {}", view.TotalSize());
			ProcessSnapshot(view);
			break;
		}

		default: break;
		}
	}

	bool ClientPhysicalWorld::AddMember(WorldUserContext user)
	{
		const uint64 participantUserId = user.userId;
		const bool added = WorldMembershipHost::AddMember(user);
		if (added)
			PublishWorldParticipantEvent(participantUserId, eWorldParticipantChange::Joined);
		return added;
	}

	bool ClientPhysicalWorld::RemoveMember(uint64 userId)
	{
		const bool removed = WorldMembershipHost::RemoveMember(userId);
		if (removed)
			PublishWorldParticipantEvent(userId, eWorldParticipantChange::Left);
		return removed;
	}

	bool ClientPhysicalWorld::RegisterWorldRouting()
	{
		if (GetWorldId() == kInvalidNetWorldId)
			return false;

		auto shard = m_shard.lock();
		if (!shard)
			return false;

		if (GetLocalWorldId() == kInvalidLocalWorldId)
		{
			const LocalWorldId allocated = InvokeOnShard(shard, [](ShardLocal& local)
				{
					auto& state = GetOrCreateWorldShardState(local);
					return state.AllocLocalWorldId();
				}, eJobPriority::Control);

			SetLocalWorldId(allocated);
		}
		if (GetLocalWorldId() == kInvalidLocalWorldId)
			return false;

		if (!m_mailboxRef.IsValid() || m_mailboxRef.ownerId != GetLocalWorldId())
			m_mailboxRef = shard->CreateMailboxRef(GetLocalWorldId());

		if (!m_mailboxRef.IsValid())
			return false;

		return true;
	}

	void ClientPhysicalWorld::SpawnActor(SpawnParams params)
	{
		JAM_ASSERT(IsCurrentShardContext());
		if (m_headless)
		{
			RequestSpawnActor(params);
			return;
		}

		SpawnActorImpl(params);
	}

	void ClientPhysicalWorld::DespawnActor(NetId netId)
	{
		RequestDespawnActor(netId);
	}

	void ClientPhysicalWorld::PushInput(uint32 inputFlags, float facingYaw, float facingPitch, uint32 commandEpoch)
	{
		m_latestLocalCommandEpoch.store(commandEpoch, std::memory_order_release);
		if (auto* inputSys = m_registry.ctx().find<ClientInputSystem>())
		{
			inputSys->SetInput(inputFlags, facingYaw, facingPitch, commandEpoch);
		}
	}

	void ClientPhysicalWorld::PushInput(const px::CharacterInput& input)
	{
		m_latestLocalCommandEpoch.store(input.commandEpoch, std::memory_order_release);
		if (auto* inputSys = m_registry.ctx().find<ClientInputSystem>())
			inputSys->SetInput(input);
	}

	void ClientPhysicalWorld::SetLatestClickMoveSeq(uint64 requestSeq)
	{
		m_latestClickMoveSeq.store(requestSeq, std::memory_order_release);
	}

	void ClientPhysicalWorld::RequestClickMove(const px::Vec3& from, const px::Vec3& dir, float maxRange, uint64 requestSeq, uint32 commandEpoch, float facingYaw)
	{
		JAM_ASSERT(IsCurrentShardContext());
		SetLatestClickMoveSeq(requestSeq);

		if (requestSeq != m_latestClickMoveSeq.load(std::memory_order_acquire))
			return;

		px::HitscanResult hit{};
		if (m_physics)
			hit = m_physics->Hitscan(from, dir, maxRange, 0);

		if (requestSeq != m_latestClickMoveSeq.load(std::memory_order_acquire))
			return;

		if (!hit.hit)
			return;

		px::CharacterInput clickInput{};
		clickInput.moveMode     = px::eMoveInputMode::Mouse;
		clickInput.commandEpoch = commandEpoch;
		clickInput.facingYaw    = facingYaw;
		clickInput.facingPitch  = 0.0f;

		bool followTarget = false;

		if (hit.hitId != px::INVALID_OBJ_ID)
		{
			const entt::entity e = static_cast<entt::entity>(hit.hitId);
			if (e != entt::null && m_registry.valid(e) && m_registry.all_of<NetId>(e))
			{
				const NetId targetNetId = m_registry.get<NetId>(e);
				if (targetNetId.IsRuntime())
				{
					clickInput.mouseMoveKind = px::eMouseMoveKind::FollowTarget;
					clickInput.targetNetId = targetNetId.Raw();
					followTarget = true;
				}
			}
		}

		if (!followTarget)
		{
			clickInput.mouseMoveKind = px::eMouseMoveKind::ToPosition;
			clickInput.targetPos     = hit.position;
		}

		if (requestSeq != m_latestClickMoveSeq.load(std::memory_order_acquire))
			return;

		PushInput(clickInput);

		ClickMoveResolvedEvent event{};
		event.accountId     = m_accountId;
		event.userId        = m_userId;
		event.requestSeq    = requestSeq;
		event.followTarget  = followTarget;
		event.targetPos     = hit.position;
		GLOBAL_EVENTBUS_PUBLISH(event);
	}

	bool ClientPhysicalWorld::TryGetNetIdFromObjectId(px::ObjectId objectId, OUT NetId& outNetId)
	{
		const entt::entity e = static_cast<entt::entity>(objectId);
		if (e == entt::null || !m_registry.valid(e))
			return false;
		if (!m_registry.all_of<NetId>(e))
			return false;

		outNetId = m_registry.get<NetId>(e);
		return outNetId.IsValid();
	}

	void ClientPhysicalWorld::RequestHitscan(const px::Vec3& from, const px::Vec3& dir, float maxRange, std::function<void(const px::HitscanResult&)> onDone)
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


	void ClientPhysicalWorld::RequestSpawnActor(const SpawnParams& params)
	{
		if (!params.desc.prefab.IsValid())
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

		const auto root = fb::CreatefbSpawnActorReq(
			fbb,
			GetWorldId(),
			params.spawnId,
			params.owned ? m_userId : 0,
			params.controlled ? m_userId : 0,
			params.desc.prefab.value,
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
			params.targetNetId.Raw());
		fbb.Finish(root);

		JAMNET_LOG_DEBUG("[ClientPhysicalWorld::OnSpawnActorRequest] account id= {}, user id= {} request spawn actor", m_accountId, m_userId);

		RPCCallOptions opt{ .channel = eChannel::RELIABLE_ORDERED, .timeout_ns = 10_s };
		RPCCallAsyncMember<fb::fbSpawnActorReq, fb::fbSpawnActorRes>(
			m_sessions.TryGetUdp(),
			fbb.GetBufferPointer(),
			static_cast<uint32>(fbb.GetSize()),
			opt,
			this,
			&ClientPhysicalWorld::OnSpawnActorResponse);
	}

	void ClientPhysicalWorld::RequestDespawnActor(NetId netId)
	{
		flatbuffers::FlatBufferBuilder fbb(64);
		const auto root = fb::CreatefbDespawnActorReq(fbb, GetWorldId(), netId.Raw());
		fbb.Finish(root);

		RPCCallOptions opt{ .channel = eChannel::RELIABLE_ORDERED, .timeout_ns = 1_s };
		RPCCallAsyncMember<fb::fbDespawnActorReq, fb::fbDespawnActorRes>(
			m_sessions.TryGetUdp(),
			fbb.GetBufferPointer(),
			static_cast<uint32>(fbb.GetSize()),
			opt,
			this,
			&ClientPhysicalWorld::OnDespawnActorResponse);
	}

	void ClientPhysicalWorld::SetReplicatedActorDormant(NetId netId)
	{
		SetActorDormantImpl(netId);
	}

	void ClientPhysicalWorld::PredictReplicatedActorDespawn(NetId netId)
	{
		const auto e = GetEntity(netId);
		if (e == entt::null || !m_registry.valid(e) || m_registry.all_of<PredictedDespawnTag>(e))
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
		m_registry.emplace_or_replace<PredictedDespawnTag>(e);

		if (wasActive)
			PublishActorDespawned(e, eActorLifecycleReason::PredictedDespawn);
	}

	void ClientPhysicalWorld::ReactivateReplicatedActor(NetId netId, bool isLocal)
	{
		const auto e = GetEntity(netId);
		if (e == entt::null || !m_registry.valid(e))
			return;

		const bool hiddenByAoi = m_registry.all_of<OutOfAoiTag>(e);
		const bool hiddenByPrediction = m_registry.all_of<PredictedDespawnTag>(e);
		if (!hiddenByAoi && !hiddenByPrediction)
			return;

		m_registry.remove<OutOfAoiTag>(e);
		m_registry.remove<PredictedDespawnTag>(e);
		PublishActorSpawned(e, 0, isLocal, eActorLifecycleReason::AoiEntered);
	}

	void ClientPhysicalWorld::DestroyReplicatedActor(NetId netId)
	{
		DespawnActorImpl(netId);
	}

	void ClientPhysicalWorld::RequestPossessActor(NetId netId)
	{
		flatbuffers::FlatBufferBuilder fbb(64);
		const auto root = fb::CreatefbPossessActorReq(fbb, GetWorldId(), netId.Raw());
		fbb.Finish(root);
		RPCCallOptions opt{ .channel = eChannel::RELIABLE_ORDERED, .timeout_ns = 1_s };
		RPCCallAsyncMember<fb::fbPossessActorReq, fb::fbPossessActorRes>(
			m_sessions.TryGetUdp(),
			fbb.GetBufferPointer(),
			static_cast<uint32>(fbb.GetSize()),
			opt,
			this,
			&ClientPhysicalWorld::OnPossessActorResponse);
	}

	void ClientPhysicalWorld::RequestUnpossessActor(NetId netId)
	{
		flatbuffers::FlatBufferBuilder fbb(64);
		const auto root = fb::CreatefbUnpossessActorReq(fbb, GetWorldId(), netId.Raw());
		fbb.Finish(root);
		RPCCallOptions opt{ eChannel::RELIABLE_ORDERED, 1_s };
		RPCCallAsyncMember<fb::fbUnpossessActorReq, fb::fbUnpossessActorRes>(
			m_sessions.TryGetUdp(),
			fbb.GetBufferPointer(),
			static_cast<uint32>(fbb.GetSize()),
			opt,
			this,
			&ClientPhysicalWorld::OnUnpossesActorResponse);
	}


	entt::entity ClientPhysicalWorld::EnsureReplicatedActor(NetId netId, px::PrefabKey prefabKey, uint64 owner, uint64 controller, px::eBodyType bodyType)
	{
		if (auto it = m_netIdToEntity.find(netId); it != m_netIdToEntity.end())
		{
			if (it->second != entt::null && m_registry.valid(it->second))
			{
				return it->second;
			}
			m_netIdToEntity.erase(it);
		}

		entt::entity e = m_registry.create();

		m_registry.emplace<NetId>(e, netId);
		m_registry.emplace<NetPrefabKey>(e, NetPrefabKey{ prefabKey });
		m_registry.emplace<NetTeamPartRole>(e);
		m_registry.emplace<OwnershipTag>(e, OwnershipTag{ owner });
		m_registry.emplace<ControlTag>(e, ControlTag{ controller });
		m_registry.emplace<NetActorBodyType>(e, NetActorBodyType{ bodyType });
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

		if (m_physics && m_physics->IsReplayCandidate(prefabKey))
		{
			m_registry.emplace<ReplayCandidateTag>(e);
			m_registry.emplace<ReplayRetention>(e, ReplayRetention{});
		}

		m_netIdToEntity.emplace(netId, e);
		PublishActorSpawned(e, 0, false);

		return e;
	}

	entt::entity ClientPhysicalWorld::TryConfirmPendingSpawn(NetId netId, uint32 spawnReqId)
	{
		if (spawnReqId == 0 || !netId.IsValid())
			return entt::null;

		auto it = m_spawnReqIdToEntity.find(spawnReqId);
		if (it == m_spawnReqIdToEntity.end())
			return entt::null;

		const entt::entity e = it->second;
		m_spawnReqIdToEntity.erase(it);

		if (e == entt::null || !m_registry.valid(e))
			return entt::null;

		m_registry.emplace_or_replace<NetId>(e, netId);

		if (m_registry.all_of<NetPendingSpawnTag>(e)) m_registry.remove<NetPendingSpawnTag>(e);
		if (m_registry.all_of<NetSpawnRequestId>(e))  m_registry.remove<NetSpawnRequestId>(e);

		m_netIdToEntity[netId] = e;

		const uint64 owner      = m_registry.get<OwnershipTag>(e).userId;
		const uint64 controller = m_registry.get<ControlTag>(e).userId;

		bool isLocal = (owner == controller) && (controller == m_userId);
		if (isLocal) m_localNetId = netId;

		PublishActorSpawned(e, spawnReqId, isLocal);

		return e;
	}



	void ClientPhysicalWorld::Tick()
	{
		m_registry.ctx().get<TickCounter>().Tick();
		m_registry.ctx().get<ClientInputSystem>().Tick();

		if (m_headless) return;

		m_registry.ctx().get<ClientReplicationSystem>().Tick();
		m_registry.ctx().get<ClientReplaySystem>().Tick();
		m_registry.ctx().get<ClientPhysicsSystem>().Tick();
		m_registry.ctx().get<ClientSamplingSystem>().Tick();
	}

	void ClientPhysicalWorld::ProcessLifecyclePacket(const PacketHeaderView& view)
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

	void ClientPhysicalWorld::ProcessSnapshot(const PacketHeaderView& view)
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


	void ClientPhysicalWorld::OnSpawnActorResponse(std::optional<RPCTableRef<fb::fbSpawnActorRes>> res)
	{
		if (!res)
		{
			JAMNET_LOG_WARN_LOC("Spawn RPC timeout or connection lost\n");
			return;
		}

		const uint64 spawnReqId = (*res)->spawn_req_id();

		if (!(*res)->success())
		{
			if (auto it = m_spawnReqIdToEntity.find(spawnReqId); it != m_spawnReqIdToEntity.end())
			{
				entt::entity e = it->second;
				m_spawnReqIdToEntity.erase(it);
				if (e != entt::null && m_registry.valid(e))
					m_registry.destroy(e);
			}
		}

	}

	void ClientPhysicalWorld::OnDespawnActorResponse(std::optional<RPCTableRef<fb::fbDespawnActorRes>> res)
	{
		if (!res.has_value())
		{
			JAMNET_LOG_WARN_LOC("Despawn RPC timeout or connection lost\n");
			return;
		}

		if (!(*res)->success())
		{
			JAMNET_LOG_WARN_LOC("Despawn RPC failed on server\n");
			return;
		}
	}

	void ClientPhysicalWorld::OnPossessActorResponse(std::optional<RPCTableRef<fb::fbPossessActorRes>> res)
	{
		if (!res.has_value() || !(*res)->success())
		{
			JAMNET_LOG_WARN_LOC("Possess RPC timeout or connection lost\n");
			return;
		}

		JAMNET_LOG_DEBUG("Possess RPC accepted for NetID={}\n", (*res)->net_id());
	}

	void ClientPhysicalWorld::OnUnpossesActorResponse(std::optional<RPCTableRef<fb::fbUnpossessActorRes>> res)
	{
		if (!res || !(*res)->success())
		{
			JAMNET_LOG_WARN_LOC("Unpossess failed\n");
			return;
		}

		JAMNET_LOG_DEBUG("Unpossess RPC accepted\n");
	}

	void ClientPhysicalWorld::PublishActorSpawned(entt::entity e, uint32 spawnReqId, bool isLocal, eActorLifecycleReason reason)
	{
		if (!m_registry.valid(e) || !m_registry.all_of<NetId, NetPrefabKey>(e))
			return;

		(void)spawnReqId;

		ActorLifecycleEvent event{};
		event.accountId	   = m_accountId;
		event.userId	   = m_userId;
		event.localWorldId = GetLocalWorldId();
		event.spawnReqId   = spawnReqId;
		event.netId        = m_registry.get<NetId>(e);
		event.objectId     = MakeObjectId(e);
		event.isLocal      = isLocal;
		event.reason       = reason;
		event.prefab       = m_registry.get<NetPrefabKey>(e).key;

		GLOBAL_EVENTBUS_PUBLISH(event);
	}

	void ClientPhysicalWorld::PublishActorDespawned(entt::entity e, eActorLifecycleReason reason)
	{
		if (!m_registry.valid(e) || !m_registry.all_of<NetId>(e))
			return;

		ActorLifecycleEvent event{};
		event.accountId	   = m_accountId;
		event.userId       = m_userId;
		event.localWorldId = GetLocalWorldId();
		event.netId		   = m_registry.get<NetId>(e);
		event.objectId	   = MakeObjectId(e);
		event.reason	   = reason;
		GLOBAL_EVENTBUS_PUBLISH(event);
	}

	void ClientPhysicalWorld::PublishWorldParticipantEvent(uint64 participantUserId, eWorldParticipantChange change)
	{
		if (participantUserId == 0 || GetLocalWorldId() == kInvalidLocalWorldId || !GetWorldKey().IsIssued())
			return;

		WorldParticipantEvent event{};
		event.accountId = m_accountId;
		event.userId = m_userId;
		event.change = change;
		event.participant = WorldParticipantView
		{
			.key = GetWorldKey(),
			.localWorldId = GetLocalWorldId(),
			.kind = m_config.desc.kind,
			.participantUserId = participantUserId,
		};
		GLOBAL_EVENTBUS_PUBLISH(event);
	}

	void ClientPhysicalWorld::BootstrapLevelActors()
	{
		if (!m_physics || m_levelPath.empty())
			return;

		m_levelLayerInfo = m_physics->SetLevelPath(m_levelPath);
		if (m_levelLayerInfo.totalCount == 0) 
			return;

		for (const auto& [layer, count] : m_levelLayerInfo.countPerLayer)
		{
			if (count == 0) continue;

			std::vector<px::LevelInstanceInfo> instances;
			instances.resize(count);

			std::vector<entt::entity> created;
			created.reserve(count);

			for (uint32 i = 0; i < count; ++i)
			{
				const entt::entity e = m_registry.create();
				created.push_back(e);

				instances[i].objectId = MakeObjectId(e);
			}

			if (!m_physics->LoadLevel(layer, instances))
			{
				for (const auto e : created)
				{
					if (m_registry.valid(e))
						m_registry.destroy(e);
				}
				continue;
			}

			for (const auto& inst : instances)
			{
				if (inst.objectId == px::INVALID_OBJ_ID) continue;

				const entt::entity e = static_cast<entt::entity>(inst.objectId);
				if (!m_registry.valid(e)) continue;

				const NetId nid = NetId::MakeLevel(inst.levelActorId);
				if (!nid.IsValid()) continue;

				m_registry.emplace<NetId>(e, nid);
				m_registry.emplace<NetPrefabKey>(e, NetPrefabKey{ inst.prefab });
				m_registry.emplace<OwnershipTag>(e);
				m_registry.emplace<ControlTag>(e);
				m_registry.emplace<RemoteActorTag>(e);
				m_registry.emplace<NetTeamPartRole>(e);
				m_registry.emplace<PhysicsSpawnedTag>(e);
				m_registry.emplace<NetActorBodyType>(e, NetActorBodyType{ .body = px::eBodyType::Rigid });
				m_registry.emplace<RigidAuthorityState>(e, inst.state);
				m_registry.emplace<RigidProxyState>(e, inst.state);
				m_registry.emplace<RigidReplayHistory>(e);

				if (m_physics->IsReplayCandidate(inst.prefab))
				{
					m_registry.emplace<ReplayCandidateTag>(e);
					m_registry.emplace<ReplayRetention>(e, ReplayRetention{});
				}

				m_netIdToEntity[nid] = e;
				PublishActorSpawned(e, 0, false, eActorLifecycleReason::Spawned);
			}
		}
	}

	void ClientPhysicalWorld::SpawnActorImpl(SpawnParams params)
	{
		entt::entity e = m_registry.create();

		m_registry.emplace<NetPendingSpawnTag>(e);
		m_registry.emplace<NetSpawnRequestId>(e, NetSpawnRequestId{ params.spawnId });
		m_registry.emplace<NetId>(e, NetId::Invalid());             // pre-creating NetId to invalid val. actual value is initialized when receive server snapshot. 
		m_registry.emplace<NetPrefabKey>(e, NetPrefabKey{ params.desc.prefab });
		m_registry.emplace<OwnershipTag>(e, OwnershipTag{ params.owned ? m_userId : 0 });
		m_registry.emplace<ControlTag>(e, ControlTag{ params.controlled ? m_userId : 0 });
		m_registry.emplace<NetTeamPartRole>(e, NetTeamPartRole{ params.desc.team, params.desc.part, params.desc.role });
		
		const bool isRigid  = params.desc.IsRigid();
		const auto bodyType = isRigid ? px::eBodyType::Rigid : px::eBodyType::Character;
		m_registry.emplace<NetActorBodyType>(e, NetActorBodyType{ bodyType });

		// pre-creating Authority/Proxy state. actual value is initialized when receive server snapshot. 
		if (isRigid)
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

		if (m_physics && m_physics->IsReplayCandidate(params.desc.prefab))
		{
			m_registry.emplace<ReplayCandidateTag>(e);
			m_registry.emplace<ReplayRetention>(e, ReplayRetention{});
		}

		if (params.targetObjectId != px::INVALID_OBJ_ID)
		{
			TargetInfo target{};
			target.targetObjId = params.targetObjectId;
			target.targetNetId = m_registry.get<NetId>(static_cast<entt::entity>(params.targetObjectId));

			m_registry.emplace<TargetInfo>(e, target);

			params.targetNetId = target.targetNetId;
		}

		m_spawnReqIdToEntity.emplace(params.spawnId, e);

		RequestSpawnActor(params);
	}

	void ClientPhysicalWorld::SetActorDormantImpl(NetId netId)
	{
		if (netId == m_localNetId)
			return;

		const auto e = GetEntity(netId);
		if (e == entt::null || !m_registry.valid(e) || m_registry.all_of<OutOfAoiTag>(e) || m_registry.all_of<PredictedDespawnTag>(e))
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

	void ClientPhysicalWorld::DespawnActorImpl(const NetId netId)
	{
		const auto e = GetEntity(netId);

		if (e == entt::null || !m_registry.valid(e))
			return;

		const bool wasActive = !m_registry.all_of<OutOfAoiTag>(e) && !m_registry.all_of<PredictedDespawnTag>(e);

		if (const auto* req = m_registry.try_get<NetSpawnRequestId>(e))
		{
			if (auto it = m_spawnReqIdToEntity.find(req->requestId); it != m_spawnReqIdToEntity.end() && it->second == e)
				m_spawnReqIdToEntity.erase(it);
		}

		if (const auto* id = m_registry.try_get<NetId>(e))
		{
			if (auto it = m_netIdToEntity.find(*id); it != m_netIdToEntity.end() && it->second == e)
				m_netIdToEntity.erase(it);
		}

		if (m_registry.all_of<PhysicsSpawnedTag>(e))
		{
			if (m_registry.ctx().contains<ClientPhysicsSystem>())
				m_registry.ctx().get<ClientPhysicsSystem>().DespawnActor(e);
			else
				m_registry.erase<PhysicsSpawnedTag>(e);
		}

		if (netId == m_localNetId)
			m_localNetId = NetId::Invalid();

		if (wasActive)
			PublishActorDespawned(e, eActorLifecycleReason::Despawned);

		m_registry.destroy(e);
	}

	bool ClientPhysicalWorld::BootstrapLocalRoute()
	{
		if (GetLocalWorldId() != kInvalidLocalWorldId)
		{
			m_localShardIndex = GetLocalWorldShardIndex(GetLocalWorldId());
			m_localWorldIndex = GetLocalWorldLocalIndex(GetLocalWorldId());
		}
		else if (m_localShardIndex == std::numeric_limits<uint16>::max())
		{
			const uint64 routeSeed = (m_config.key.worldId != kInvalidNetWorldId)
				? m_config.key.worldId
				: (static_cast<uint64>(m_config.key.descId) << 32) ^ m_userId;

			const RouteAssignment assignment = GLOBAL_EXEC.PlaceRoute(
				GLOBAL_EXEC.MakeRouteKey(kClientWorldRouteDomain, routeSeed));
			if (!IsValidRouteAssignment(assignment))
				return false;

			m_localShardIndex = assignment.shardIndex;
		}

		auto shard = m_shard.lock();
		if (!shard)
			shard = GLOBAL_EXEC.GetShardFromIndex(m_localShardIndex);
		if (!shard)
			return false;

		m_shard = shard;
		m_alive.store(true, std::memory_order_relaxed);
		if (m_localWorldIndex == 0)
			m_localWorldIndex = NextClientWorldLocalIndex();

		return true;
	}
}
