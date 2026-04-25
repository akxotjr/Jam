#include "pch.h"
#include "jamnet/sync/networld/ClientNetWorld.h"

#include "jamnet/core/executor/GlobalEventBus.h"
#include "jamnet/core/executor/MainExecutor.h"
#include "jamnet/core/net/Session.h"

#include "jamnet/sync/transport/CustomPacketHelper.h"
#include "jamnet/sync/replication/NetActorComponents.h"
#include "jamnet/sync/replication/NetWorldContext.h"
#include "jamnet/sync/replication/ClientInputSystem.h"
#include "jamnet/sync/replication/ClientReplaySystem.h"
#include "jamnet/sync/replication/ClientReplicationSystem.h"
#include "jamnet/sync/replication/ClientPhysicsSystem.h"
#include "jamnet/sync/replication/ClientSamplingSystem.h"
#include "jamnet/sync/replication/ReplicationEvents.h"

#include "jamnet/sync/schema/gen/lifecycle_generated.h"
#include "jamnet/sync/schema/gen/snapshot_generated.h"

namespace jam::net
{
	void ClientNetWorld::Init()
	{
		NetWorld::Init();

		if (!m_transport) return;

		const bool usePhysics = !m_headless && m_physics != nullptr;
		if (usePhysics)
		{
			m_physics->SetJobBridge(m_bridge.get());
			m_physics->Init();
		}

		InitClientNetWorldCtx(m_world);

		m_world.ctx().emplace<ClientNetWorld*>(this);
		m_world.ctx().emplace<TickCounter>().Init();
		m_world.ctx().emplace<ClientInputSystem>(m_world).Init();
		m_world.ctx().emplace<ClientReplicationSystem>(m_world).Init();
		if (usePhysics)
		{
			m_world.ctx().emplace<ClientReplaySystem>(m_world).Init();
			m_world.ctx().emplace<ClientPhysicsSystem>(m_world, m_physics.get()).Init();
			m_world.ctx().emplace<ClientSamplingSystem>(m_world).Init();

			BootstrapLevelActors();
		}
	}

	void ClientNetWorld::SetTransportSystem(std::shared_ptr<ITransportEndpoint> transport)
	{
		m_transport = std::move(transport);
	}

	void ClientNetWorld::SetPhysicsFacade(std::unique_ptr<px::IPhysicsFacade> physics)
	{
		m_physics = std::move(physics);
	}

 
	entt::entity ClientNetWorld::GetEntity(NetId netId)
	{
		if (auto it = m_netIdToEntity.find(netId); it != m_netIdToEntity.end())
		{
			return it->second;
		}
		return entt::null;
	}


	void ClientNetWorld::Send(Packet packet)
	{
		if (m_transport) m_transport->Send({}, packet);
	}

	void ClientNetWorld::OnRecvPacket(const PacketHeaderView& view)
	{
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

	void ClientNetWorld::SpawnActor(SpawnParams params)
	{
		Post(Job([this, params = params]()
			{
				SpawnActorImpl(params);
			}));
	}

	void ClientNetWorld::DespawnActor(NetId netId)
	{
		RequestDespawnActor(netId);
	}

	void ClientNetWorld::PushInput(uint32 inputFlags, float facingYaw, float facingPitch, uint32 commandEpoch)
	{
		m_latestLocalCommandEpoch.store(commandEpoch, std::memory_order_release);
		if (auto* inputSys = m_world.ctx().find<ClientInputSystem>())
		{
			inputSys->SetInput(inputFlags, facingYaw, facingPitch, commandEpoch);
		}
	}

	void ClientNetWorld::PushInput(const px::CharacterInput& input)
	{
		m_latestLocalCommandEpoch.store(input.commandEpoch, std::memory_order_release);
		if (auto* inputSys = m_world.ctx().find<ClientInputSystem>())
			inputSys->SetInput(input);
	}

	void ClientNetWorld::SetLatestClickMoveSeq(uint64 requestSeq)
	{
		m_latestClickMoveSeq.store(requestSeq, std::memory_order_release);
	}

	void ClientNetWorld::RequestClickMove(const px::Vec3& from, const px::Vec3& dir, float maxRange, uint64 requestSeq, uint32 commandEpoch, float facingYaw)
	{
		SetLatestClickMoveSeq(requestSeq);

		Post(Job([this, from, dir, maxRange, requestSeq, commandEpoch, facingYaw]()
			{
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
					if (e != entt::null && m_world.valid(e) && m_world.all_of<NetId>(e))
					{
						const NetId targetNetId = m_world.get<NetId>(e);
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
				event.userId        = m_userId;
				event.requestSeq    = requestSeq;
				event.followTarget  = followTarget;
				event.targetPos     = hit.position;
				GLOBAL_EVENTBUS_PUBLISH(event);
			}));
	}

	bool ClientNetWorld::TryGetNetIdFromObjectId(px::ObjectId objectId, OUT NetId& outNetId)
	{
		const entt::entity e = static_cast<entt::entity>(objectId);
		if (e == entt::null || !m_world.valid(e))
			return false;
		if (!m_world.all_of<NetId>(e))
			return false;

		outNetId = m_world.get<NetId>(e);
		return outNetId.IsValid();
	}

	void ClientNetWorld::RequestHitscan(const px::Vec3& from, const px::Vec3& dir, float maxRange, std::function<void(const px::HitscanResult&)> onDone)
	{
		Post(Job([this, from, dir, maxRange, onDone = std::move(onDone)]() mutable
			{
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
			}));
	}


	void ClientNetWorld::RequestSpawnActor(const SpawnParams& params)
	{
		if (!m_transport || !params.desc.prefab.IsValid())
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

		RPCCallOptions opt{ .channel = eChannel::RELIABLE_ORDERED, .timeout_ns = 10_s };
		m_transport->RPCCallAwaitMember<fb::fbSpawnActorReq, fb::fbSpawnActorRes>(m_userId, eProtocolType::UDP, fbb.GetBufferPointer(), static_cast<uint32>(fbb.GetSize()), opt, this, &ClientNetWorld::OnSpawnActorResponse);
	}

	void ClientNetWorld::RequestDespawnActor(NetId netId)
	{
		if (!m_transport)
			return;

		flatbuffers::FlatBufferBuilder fbb(64);
		const auto root = fb::CreatefbDespawnActorReq(fbb, netId.Raw());
		fbb.Finish(root);

		RPCCallOptions opt{ .channel = eChannel::RELIABLE_ORDERED, .timeout_ns = 1_s };
		m_transport->RPCCallAwaitMember<fb::fbDespawnActorReq, fb::fbDespawnActorRes>(m_userId, eProtocolType::UDP, fbb.GetBufferPointer(), static_cast<uint32>(fbb.GetSize()), opt, this, &ClientNetWorld::OnDespawnActorResponse);
	}

	void ClientNetWorld::SetReplicatedActorDormant(NetId netId)
	{
		SetActorDormantImpl(netId);
	}

	void ClientNetWorld::PredictReplicatedActorDespawn(NetId netId)
	{
		const auto e = GetEntity(netId);
		if (e == entt::null || !m_world.valid(e) || m_world.all_of<PredictedDespawnTag>(e))
			return;

		const bool wasActive = !m_world.all_of<OutOfAoiTag>(e);

		if (m_world.all_of<PhysicsSpawnedTag>(e))
		{
			if (m_world.ctx().contains<ClientPhysicsSystem>())
				m_world.ctx().get<ClientPhysicsSystem>().DespawnActor(e);
			else
				m_world.erase<PhysicsSpawnedTag>(e);
		}

		m_world.remove<ReplayRelevantTag>(e);
		m_world.emplace_or_replace<PredictedDespawnTag>(e);

		if (wasActive)
			PublishActorDespawned(e, eRenderActorLifecycleReason::PredictedDespawn);
	}

	void ClientNetWorld::ReactivateReplicatedActor(NetId netId, bool isLocal)
	{
		const auto e = GetEntity(netId);
		if (e == entt::null || !m_world.valid(e))
			return;

		const bool hiddenByAoi = m_world.all_of<OutOfAoiTag>(e);
		const bool hiddenByPrediction = m_world.all_of<PredictedDespawnTag>(e);
		if (!hiddenByAoi && !hiddenByPrediction)
			return;

		m_world.remove<OutOfAoiTag>(e);
		m_world.remove<PredictedDespawnTag>(e);
		PublishActorSpawned(e, 0, isLocal, eRenderActorLifecycleReason::AoiEntered);
	}

	void ClientNetWorld::DestroyReplicatedActor(NetId netId)
	{
		DespawnActorImpl(netId);
	}

	void ClientNetWorld::RequestPossessActor(NetId netId)
	{
		if (!m_transport) return;

		flatbuffers::FlatBufferBuilder fbb(64);
		const auto root = fb::CreatefbPossessActorReq(fbb, netId.Raw());
		fbb.Finish(root);
		RPCCallOptions opt{ .channel = eChannel::RELIABLE_ORDERED, .timeout_ns = 1_s };
		m_transport->RPCCallAwaitMember<fb::fbPossessActorReq, fb::fbPossessActorRes>(m_userId, eProtocolType::UDP, fbb.GetBufferPointer(), static_cast<uint32>(fbb.GetSize()), opt, this, &ClientNetWorld::OnPossessActorResponse);
	}

	void ClientNetWorld::RequestUnpossessActor(NetId netId)
	{
		if (!m_transport) return;

		flatbuffers::FlatBufferBuilder fbb(64);
		const auto root = fb::CreatefbUnpossessActorReq(fbb, netId.Raw());
		fbb.Finish(root);
		RPCCallOptions opt{ eChannel::RELIABLE_ORDERED, 1_s };
		m_transport->RPCCallAwaitMember<fb::fbUnpossessActorReq, fb::fbUnpossessActorRes>(m_userId, eProtocolType::UDP, fbb.GetBufferPointer(), static_cast<uint32>(fbb.GetSize()), opt, this, &ClientNetWorld::OnUnpossesActorResponse);
	}


	entt::entity ClientNetWorld::EnsureReplicatedActor(NetId netId, px::PrefabKey prefabKey, uint64 owner, uint64 controller, px::eBodyType bodyType)
	{
		if (auto it = m_netIdToEntity.find(netId); it != m_netIdToEntity.end())
		{
			if (it->second != entt::null && m_world.valid(it->second))
			{
				return it->second;
			}
			m_netIdToEntity.erase(it);
		}

		entt::entity e = m_world.create();

		m_world.emplace<NetId>(e, netId);
		m_world.emplace<NetPrefabKey>(e, NetPrefabKey{ prefabKey });
		m_world.emplace<NetTeamPartRole>(e);
		m_world.emplace<OwnershipTag>(e, OwnershipTag{ owner });
		m_world.emplace<ControlTag>(e, ControlTag{ controller });
		m_world.emplace<NetActorBodyType>(e, NetActorBodyType{ bodyType });
		if (bodyType == px::eBodyType::Rigid)
		{
			m_world.emplace<RigidAuthorityState>(e);
			m_world.emplace<RigidProxyState>(e);
			m_world.emplace<RigidReplayHistory>(e);
		}
		else
		{
			m_world.emplace<CharAuthorityState>(e);
			m_world.emplace<CharProxyState>(e);
			m_world.emplace<CharReplayHistory>(e);
		}

		if (m_physics && m_physics->IsReplayCandidate(prefabKey))
		{
			m_world.emplace<ReplayCandidateTag>(e);
			m_world.emplace<ReplayRetention>(e, ReplayRetention{});
		}

		m_netIdToEntity.emplace(netId, e);
		PublishActorSpawned(e, 0, false);

		return e;
	}

	entt::entity ClientNetWorld::TryConfirmPendingSpawn(NetId netId, uint32 spawnReqId)
	{
		if (spawnReqId == 0 || !netId.IsValid())
			return entt::null;

		auto it = m_spawnReqIdToEntity.find(spawnReqId);
		if (it == m_spawnReqIdToEntity.end())
			return entt::null;

		const entt::entity e = it->second;
		m_spawnReqIdToEntity.erase(it);

		if (e == entt::null || !m_world.valid(e))
			return entt::null;

		m_world.emplace_or_replace<NetId>(e, netId);

		if (m_world.all_of<NetPendingSpawnTag>(e)) m_world.remove<NetPendingSpawnTag>(e);
		if (m_world.all_of<NetSpawnRequestId>(e))  m_world.remove<NetSpawnRequestId>(e);

		m_netIdToEntity[netId] = e;

		const uint64 owner      = m_world.get<OwnershipTag>(e).userId;
		const uint64 controller = m_world.get<ControlTag>(e).userId;

		bool isLocal = (owner == controller) && (controller == m_userId);
		if (isLocal) m_localNetId = netId;

		PublishActorSpawned(e, spawnReqId, isLocal);

		return e;
	}



	void ClientNetWorld::TickOnShard()
	{
		if (!m_world.ctx().contains<TickCounter>()
			|| !m_world.ctx().contains<ClientInputSystem>()
			|| !m_world.ctx().contains<ClientReplicationSystem>())
			return;

		m_world.ctx().get<TickCounter>().Tick();
		m_world.ctx().get<ClientInputSystem>().Tick();
		m_world.ctx().get<ClientReplicationSystem>().Tick();

		if (m_world.ctx().contains<ClientReplaySystem>())
			m_world.ctx().get<ClientReplaySystem>().Tick();
		if (m_world.ctx().contains<ClientPhysicsSystem>())
			m_world.ctx().get<ClientPhysicsSystem>().Tick();
		if (m_world.ctx().contains<ClientSamplingSystem>())
			m_world.ctx().get<ClientSamplingSystem>().Tick();
	}

	void ClientNetWorld::ProcessLifecyclePacket(const PacketHeaderView& view)
	{
		flatbuffers::Verifier verifier(view.Payload(), view.PayloadSize());
		if (!fb::VerifyfbLifecycleBatchBuffer(verifier))
			return;

		auto fbBatch = fb::UnPackfbLifecycleBatch(view.Payload());
		if (!fbBatch) return;

		auto batch = std::make_shared<fb::fbLifecycleBatchT>(std::move(*fbBatch));

		Post(Job([this, batch]()
			{
				if (auto* repl = m_world.ctx().find<ClientReplicationSystem>())
					repl->EnqueueLifecycle(std::move(*batch));
			}));
	}

	void ClientNetWorld::ProcessSnapshot(const PacketHeaderView& view)
	{
		flatbuffers::Verifier verifier(view.Payload(), view.PayloadSize());
		if (!fb::VerifyfbSnapshotBuffer(verifier))
			return;

		const uint64 recvNs = NOW_NS();

		auto fbSnap = fb::UnPackfbSnapshot(view.Payload());
		if (!fbSnap) return;

		auto snap = std::make_shared<fb::fbSnapshotT>(std::move(*fbSnap));

		Post(Job([this, snap, recvNs]()
			{
				if (auto* repl = m_world.ctx().find<ClientReplicationSystem>())
				{
					repl->EnqueueSnapshot(std::move(*snap), recvNs);
				}
			}));
	}


	void ClientNetWorld::OnSpawnActorResponse(std::optional<RPCTableRef<fb::fbSpawnActorRes>> res)
	{
		if (!res)
		{
			JAMNET_LOG_WARN_LOC("Spawn RPC timeout or connection lost\n");
			return;
		}

		const NetId  nid        = NetId::MakeRaw((*res)->net_id());
		const uint64 spawnReqId = (*res)->spawn_req_id();

		JAMNET_LOG_DEBUG("OnSpawnActorResponse: NetId= {}, SpawnReqId= {}", nid.Raw(), spawnReqId);

		if (!(*res)->success())
		{
			if (auto it = m_spawnReqIdToEntity.find(spawnReqId); it != m_spawnReqIdToEntity.end())
			{
				entt::entity e = it->second;
				m_spawnReqIdToEntity.erase(it);
				if (e != entt::null && m_world.valid(e))
					m_world.destroy(e);
			}
			return;
		}

		JAMNET_LOG_DEBUG("Spawn RPC accepted; waiting for lifecycle create. NetId= {}, SpawnReqId= {}", nid.Raw(), spawnReqId);
	}

	void ClientNetWorld::OnDespawnActorResponse(std::optional<RPCTableRef<fb::fbDespawnActorRes>> res)
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

	void ClientNetWorld::OnPossessActorResponse(std::optional<RPCTableRef<fb::fbPossessActorRes>> res)
	{
		if (!res.has_value() || !(*res)->success())
		{
			JAMNET_LOG_WARN_LOC("Possess RPC timeout or connection lost\n");
			return;
		}

		JAMNET_LOG_DEBUG("Possess RPC accepted for NetID={}\n", (*res)->net_id());
	}

	void ClientNetWorld::OnUnpossesActorResponse(std::optional<RPCTableRef<fb::fbUnpossessActorRes>> res)
	{
		if (!res || !(*res)->success())
		{
			JAMNET_LOG_WARN_LOC("Unpossess failed\n");
			return;
		}

		JAMNET_LOG_DEBUG("Unpossess RPC accepted\n");
	}

	void ClientNetWorld::PublishActorSpawned(entt::entity e, uint32 spawnReqId, bool isLocal, eRenderActorLifecycleReason reason)
	{
		if (!m_world.valid(e) || !m_world.all_of<NetId, NetPrefabKey>(e))
			return;

		RenderActorSpawnedEvent event{};
		event.userId        = m_userId;
		event.spawnReqId    = spawnReqId;
		event.netId         = m_world.get<NetId>(e).Raw();
		event.objectId      = MakeObjectId(e);
		event.isLocal       = isLocal;
		event.reason        = reason;
		event.prefab        = m_world.get<NetPrefabKey>(e).key;

		GLOBAL_EVENTBUS_PUBLISH(event);
	}

	void ClientNetWorld::PublishActorDespawned(entt::entity e, eRenderActorLifecycleReason reason)
	{
		if (!m_world.valid(e) || !m_world.all_of<NetId>(e))
			return;

		RenderActorDespawnedEvent event{};
		event.userId   = m_userId;
		event.netId    = m_world.get<NetId>(e).Raw();
		event.objectId = MakeObjectId(e);
		event.reason   = reason;
		GLOBAL_EVENTBUS_PUBLISH(event);
	}

	void ClientNetWorld::BootstrapLevelActors()
	{
		if (!m_physics || m_levelPath.empty())
			return;

		m_levelLayerInfo = m_physics->SetLevelPath(m_levelPath);
		if (m_levelLayerInfo.totalCount == 0) 
			return;

		RenderLevelSpawnedEvent event{};
		event.userId = m_userId;

		bool hasAny = false;

		for (const auto& [layer, count] : m_levelLayerInfo.countPerLayer)
		{
			if (count == 0) continue;

			std::vector<px::LevelInstanceInfo> instances;
			instances.resize(count);

			std::vector<entt::entity> created;
			created.reserve(count);

			for (uint32 i = 0; i < count; ++i)
			{
				const entt::entity e = m_world.create();
				created.push_back(e);

				instances[i].objectId = MakeObjectId(e);
			}

			if (!m_physics->LoadLevel(layer, instances))
			{
				for (const auto e : created)
				{
					if (m_world.valid(e))
						m_world.destroy(e);
				}
				continue;
			}

			for (const auto& inst : instances)
			{
				if (inst.objectId == px::INVALID_OBJ_ID) continue;

				const entt::entity e = static_cast<entt::entity>(inst.objectId);
				if (!m_world.valid(e)) continue;

				const NetId nid = NetId::MakeLevel(inst.levelActorId);
				if (!nid.IsValid()) continue;

				m_world.emplace<NetId>(e, nid);
				m_world.emplace<NetPrefabKey>(e, NetPrefabKey{ inst.prefab });
				m_world.emplace<OwnershipTag>(e);
				m_world.emplace<ControlTag>(e);
				m_world.emplace<RemoteActorTag>(e);
				m_world.emplace<NetTeamPartRole>(e);
				m_world.emplace<PhysicsSpawnedTag>(e);
				m_world.emplace<NetActorBodyType>(e, NetActorBodyType{ .body = px::eBodyType::Rigid });
				m_world.emplace<RigidAuthorityState>(e, inst.state);
				m_world.emplace<RigidProxyState>(e, inst.state);
				m_world.emplace<RigidReplayHistory>(e);

				if (m_physics->IsReplayCandidate(inst.prefab))
				{
					m_world.emplace<ReplayCandidateTag>(e);
					m_world.emplace<ReplayRetention>(e, ReplayRetention{});
				}
			
				event.instances[inst.objectId] = inst.prefab;
				hasAny = true;

				m_netIdToEntity[nid] = e;
			}
		}

		if (hasAny)
			GLOBAL_EVENTBUS_PUBLISH(event);
	}

	void ClientNetWorld::SpawnActorImpl(SpawnParams params)
	{
		entt::entity e = m_world.create();

		m_world.emplace<NetPendingSpawnTag>(e);
		m_world.emplace<NetSpawnRequestId>(e, NetSpawnRequestId{ params.spawnId });
		m_world.emplace<NetId>(e, NetId::Invalid());             // pre-creating NetId to invalid val. actual value is initialized when receive server snapshot. 
		m_world.emplace<NetPrefabKey>(e, NetPrefabKey{ params.desc.prefab });
		m_world.emplace<OwnershipTag>(e, OwnershipTag{ params.owned ? m_userId : 0 });
		m_world.emplace<ControlTag>(e, ControlTag{ params.controlled ? m_userId : 0 });
		m_world.emplace<NetTeamPartRole>(e, NetTeamPartRole{ params.desc.team, params.desc.part, params.desc.role });
		
		const bool isRigid  = params.desc.IsRigid();
		const auto bodyType = isRigid ? px::eBodyType::Rigid : px::eBodyType::Character;
		m_world.emplace<NetActorBodyType>(e, NetActorBodyType{ bodyType });

		// pre-creating Authority/Proxy state. actual value is initialized when receive server snapshot. 
		if (isRigid)
		{
			m_world.emplace<RigidAuthorityState>(e);
			m_world.emplace<RigidProxyState>(e);
			m_world.emplace<RigidReplayHistory>(e);
		}
		else
		{
			m_world.emplace<CharAuthorityState>(e);
			m_world.emplace<CharProxyState>(e);
			m_world.emplace<CharReplayHistory>(e);
		}

		if (m_physics && m_physics->IsReplayCandidate(params.desc.prefab))
		{
			m_world.emplace<ReplayCandidateTag>(e);
			m_world.emplace<ReplayRetention>(e, ReplayRetention{});
		}

		if (params.targetObjectId != px::INVALID_OBJ_ID)
		{
			TargetInfo target{};
			target.targetObjId = params.targetObjectId;
			target.targetNetId = m_world.get<NetId>(static_cast<entt::entity>(params.targetObjectId));

			m_world.emplace<TargetInfo>(e, target);

			params.targetNetId = target.targetNetId;
		}

		m_spawnReqIdToEntity.emplace(params.spawnId, e);

		RequestSpawnActor(params);
	}

	void ClientNetWorld::SetActorDormantImpl(NetId netId)
	{
		if (netId == m_localNetId)
			return;

		const auto e = GetEntity(netId);
		if (e == entt::null || !m_world.valid(e) || m_world.all_of<OutOfAoiTag>(e) || m_world.all_of<PredictedDespawnTag>(e))
			return;

		if (m_world.all_of<PhysicsSpawnedTag>(e))
		{
			if (m_world.ctx().contains<ClientPhysicsSystem>())
				m_world.ctx().get<ClientPhysicsSystem>().DespawnActor(e);
			else
				m_world.erase<PhysicsSpawnedTag>(e);
		}

		m_world.remove<ReplayRelevantTag>(e);
		m_world.emplace_or_replace<OutOfAoiTag>(e);
		PublishActorDespawned(e, eRenderActorLifecycleReason::AoiLeft);
	}

	void ClientNetWorld::DespawnActorImpl(const NetId netId)
	{
		const auto e = GetEntity(netId);

		if (e == entt::null || !m_world.valid(e))
			return;

		const bool wasActive = !m_world.all_of<OutOfAoiTag>(e) && !m_world.all_of<PredictedDespawnTag>(e);

		if (const auto* req = m_world.try_get<NetSpawnRequestId>(e))
		{
			if (auto it = m_spawnReqIdToEntity.find(req->requestId); it != m_spawnReqIdToEntity.end() && it->second == e)
				m_spawnReqIdToEntity.erase(it);
		}

		if (const auto* id = m_world.try_get<NetId>(e))
		{
			if (auto it = m_netIdToEntity.find(*id); it != m_netIdToEntity.end() && it->second == e)
				m_netIdToEntity.erase(it);
		}

		if (m_world.all_of<PhysicsSpawnedTag>(e))
		{
			if (m_world.ctx().contains<ClientPhysicsSystem>())
				m_world.ctx().get<ClientPhysicsSystem>().DespawnActor(e);
			else
				m_world.erase<PhysicsSpawnedTag>(e);
		}

		if (netId == m_localNetId)
			m_localNetId = NetId::Invalid();

		if (wasActive)
			PublishActorDespawned(e, eRenderActorLifecycleReason::Destroyed);

		m_world.destroy(e);
	}
}
