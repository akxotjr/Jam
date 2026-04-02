#include "pch.h"

#include "jamnet/sync/networld/ClientNetWorld.h"
#include "jamnet/sync/transport/CustomPacketHelper.h"
#include "jamnet/sync/replication/NetActorComponents.h"
#include "jamnet/sync/replication/NetWorldContext.h"
#include "jamnet/sync/replication/ClientInputSystem.h"
#include "jamnet/sync/replication/ClientReplaySystem.h"
#include "jamnet/sync/replication/ClientReplicationSystem.h"
#include "jamnet/sync/replication/ClientPhysicsSystem.h"
#include "jamnet/sync/replication/ClientSamplingSystem.h"
#include "jamnet/sync/replication/ReplicationEvents.h"

#include "jamnet/sync/schema/gen/snapshot_generated.h"

namespace jam::net
{
	void ClientNetWorld::Init()
	{
		NetWorld::Init();

        if (!m_transport || !m_physics) return;

        m_physics->SetJobBridge(m_bridge.get());
        m_physics->Init();

        InitClientNetWorldCtx(m_world);

        m_world.ctx().emplace<ClientNetWorld*>(this);
        m_world.ctx().emplace<TickCounter>().Init();
        m_world.ctx().emplace<ClientInputSystem>(m_world).Init();
		m_world.ctx().emplace<ClientReplicationSystem>(m_world).Init();
        m_world.ctx().emplace<ClientReplaySystem>(m_world).Init();
        m_world.ctx().emplace<ClientPhysicsSystem>(m_world, m_physics.get()).Init();
        m_world.ctx().emplace<ClientSamplingSystem>(m_world).Init();

        BootstrapLevelActors();
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


    void ClientNetWorld::Send(const std::shared_ptr<SendBuffer>& buf)
    {
        if (m_transport) m_transport->Send({}, buf);
    }

    void ClientNetWorld::OnRecvPacket(const PacketView& view)
    {
        switch (view.Id())
        {
        case CustomPacketId::SNAPSHOT:
        {
            ProcessSnapshot(view);
            break;
        }

        default: break;
        }

        //JAMNET_LOG_DEBUG("Snapshot total size= {}, header size= {}, payload size= {}", view.TotalSize(), view.HeaderSize(), view.PayloadSize());
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

    void ClientNetWorld::PushInput(uint32 inputFlags, float facingYaw, float facingPitch)
    {
        if (auto* inputSys = m_world.ctx().find<ClientInputSystem>())
        {
            inputSys->SetInput(inputFlags, facingYaw, facingPitch);
        }
    }


    void ClientNetWorld::RequestSpawnActor(const SpawnParams& params)
    {
        if (!m_transport || !params.desc.prefab.IsValid())
            return;

        fb::fbSpawnActorReqT req{};

        req.spawn_req_id        = params.spawnId;
        req.owner_user_id       = params.owned ? m_userId : 0;
		req.controller_user_id  = params.controlled ? m_userId : 0;
        req.prefab_key          = params.desc.prefab.value;
		req.pos                 = std::make_unique<fb::fbVec3>(params.desc.pose.p.x, params.desc.pose.p.y, params.desc.pose.p.z);
		req.rot                 = std::make_unique<fb::fbQuat>(params.desc.pose.q.x, params.desc.pose.q.y, params.desc.pose.q.z, params.desc.pose.q.w);
		req.spawn_src           = static_cast<uint32>(params.desc.spawnSrc);
		req.team_id             = params.desc.team;
		req.part_id             = params.desc.part;
		req.role_id             = params.desc.role;
        req.target_net_id       = params.targetNetId.Raw();
		
		if (params.desc.IsRigid())
        {
            const auto& overrides = std::get<px::RigidSpawnOverrides>(params.desc.overrides);
			req.override_mask = overrides.mask.bits();
        
            if (overrides.mask.has_any(px::SpawnOverrideMask::LINEAR_VEL))
				req.linear_vel = std::make_unique<fb::fbVec3>(overrides.linearVelocity.x, overrides.linearVelocity.y, overrides.linearVelocity.z);
			if (overrides.mask.has_any(px::SpawnOverrideMask::ANGULAR_VEL))
				req.angular_vel = std::make_unique<fb::fbVec3>(overrides.angularVelocity.x, overrides.angularVelocity.y, overrides.angularVelocity.z);
			if (overrides.mask.has_any(px::SpawnOverrideMask::LINEAR_DAMP))
				req.linear_damping = overrides.linearDamping;
			if (overrides.mask.has_any(px::SpawnOverrideMask::ANGULAR_DAMP))
				req.angular_damping = overrides.angularDamping;
        }
        else
        {
			const auto& overrides = std::get<px::CharacterSpawnOverrides>(params.desc.overrides);
			req.override_mask = overrides.mask.bits();

			if (overrides.mask.has_any(px::SpawnOverrideMask::VIEW_YAW))
				req.yaw = overrides.yaw;
			if (overrides.mask.has_any(px::SpawnOverrideMask::VIEW_PITCH))
				req.pitch = overrides.pitch;
        }

		RPCCallOptions opt{ .channel = eChannelType::RELIABLE_ORDERED, .timeout_ns = 10_s };
        m_transport->RPCCallAwaitMember<fb::fbSpawnActorReqT, fb::fbSpawnActorResT>(m_userId, eProtocolType::UDP, std::move(req), opt, this, &ClientNetWorld::OnSpawnActorResponse);
    }

    void ClientNetWorld::RequestDespawnActor(NetId netId)
    {
        if (!m_transport)
            return;

        fb::fbDespawnActorReqT req{};
        req.net_id = netId.Raw();

        RPCCallOptions opt{ .channel = eChannelType::RELIABLE_ORDERED, .timeout_ns = 1_s };
        m_transport->RPCCallAwaitMember<fb::fbDespawnActorReqT, fb::fbDespawnActorResT>(m_userId, eProtocolType::UDP, std::move(req), opt, this, &ClientNetWorld::OnDespawnActorResponse);
    }

    void ClientNetWorld::RequestPossessActor(NetId netId)
    {
		if (!m_transport) return;

        fb::fbPossessActorReqT req{};
		req.net_id = netId.Raw();
        RPCCallOptions opt{ .channel = eChannelType::RELIABLE_ORDERED, .timeout_ns = 1_s };
		m_transport->RPCCallAwaitMember<fb::fbPossessActorReqT, fb::fbPossessActorResT>(m_userId, eProtocolType::UDP, std::move(req), opt, this, &ClientNetWorld::OnPossessActorResponse);
    }

    void ClientNetWorld::RequestUnpossessActor(NetId netId)
    {
        if (!m_transport) return;

		fb::fbUnpossessActorReqT req{};
		req.net_id = netId.Raw();
        RPCCallOptions opt{ eChannelType::RELIABLE_ORDERED, 1_s };
		m_transport->RPCCallAwaitMember<fb::fbUnpossessActorReqT, fb::fbUnpossessActorResT>(m_userId, eProtocolType::UDP, std::move(req), opt, this, &ClientNetWorld::OnUnpossesActorResponse);
    }


    entt::entity ClientNetWorld::EnsureReplicatedActor(NetId netId, px::PrefabKey prefabKey, uint64 owner, uint64 controller)
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


        const auto bodyType = m_physics->FindBodyType(prefabKey);
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

        if (m_physics->IsReplayCandidate(prefabKey))
        {
            m_world.emplace<ReplayCandidateTag>(e);
            m_world.emplace<ReplayRetention>(e, ReplayRetention{});
        }

        m_netIdToEntity.emplace(netId, e);

        RenderActorSpawnedEvent event{};
        event.userId    = m_userId;
        event.isLocal   = false;
        event.objectId  = MakeObjectId(e);
        event.prefab    = prefabKey;

        GLOBAL_EVENTBUS_PUBLISH(event);

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

        const uint64        owner      = m_world.get<OwnershipTag>(e).userId;
        const uint64        controller = m_world.get<ControlTag>(e).userId;
        const px::PrefabKey prefab     = m_world.get<NetPrefabKey>(e).key;

        bool isLocal = (owner == controller) && (controller == m_userId);
        if (isLocal) m_localNetId = netId;

        RenderActorSpawnedEvent event{};
        event.userId        = m_userId;
        event.spawnReqId    = spawnReqId;
        event.objectId      = MakeObjectId(e);
        event.isLocal       = isLocal;
        event.prefab        = prefab;

        GLOBAL_EVENTBUS_PUBLISH(event);

        return e;
    }



    void ClientNetWorld::TickOnShard()
	{
        if (!m_world.ctx().contains<TickCounter>()
            || !m_world.ctx().contains<ClientInputSystem>()
            || !m_world.ctx().contains<ClientReplicationSystem>()
            || !m_world.ctx().contains<ClientReplaySystem>()
            || !m_world.ctx().contains<ClientPhysicsSystem>()
            || !m_world.ctx().contains<ClientSamplingSystem>())
            return;

        m_world.ctx().get<TickCounter>().Tick();
        m_world.ctx().get<ClientInputSystem>().Tick();
        m_world.ctx().get<ClientReplicationSystem>().Tick();
        m_world.ctx().get<ClientReplaySystem>().Tick();
        m_world.ctx().get<ClientPhysicsSystem>().Tick();
        m_world.ctx().get<ClientSamplingSystem>().Tick();
	}

    void ClientNetWorld::ProcessSnapshot(const PacketView& view)
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


    void ClientNetWorld::OnSpawnActorResponse(std::optional<fb::fbSpawnActorResT> res)
    {
        if (!res)
        {
            JAMNET_LOG_WARN_LOC("Spawn RPC timeout or connection lost\n");
            return;
        }

        const NetId  nid        = NetId::MakeRaw(res->net_id);
        const uint64 spawnReqId = res->spawn_req_id;

        JAMNET_LOG_DEBUG("OnSpawnActorResponse: NetId= {}, SpawnReqId= {}", nid.Raw(), spawnReqId);

        if (!res->success)
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

        TryConfirmPendingSpawn(nid, spawnReqId);
    }

    void ClientNetWorld::OnDespawnActorResponse(std::optional<fb::fbDespawnActorResT> res)
    {
        if (!res.has_value())
        {
            JAMNET_LOG_WARN_LOC("Despawn RPC timeout or connection lost\n");
            return;
        }

        if (!res.value().success)
        {
            JAMNET_LOG_WARN_LOC("Despawn RPC failed on server\n");
            return;
        }


    }

    void ClientNetWorld::OnPossessActorResponse(std::optional<fb::fbPossessActorResT> res)
    {
		if (!res.has_value() || !res.value().success)
        {
            JAMNET_LOG_WARN_LOC("Possess RPC timeout or connection lost\n");
            return;
        }

		auto view = m_world.view<NetId>();
        for (auto e : view)
        {
            if (view.get<NetId>(e).Raw() == res->net_id)
            {
                m_world.emplace_or_replace<ControlTag>(e, ControlTag{ m_userId });
                JAMNET_LOG_DEBUG("Now controlling actor NetID=%llu\n", res->net_id);
                break;
            }
        }
    }

    void ClientNetWorld::OnUnpossesActorResponse(std::optional<fb::fbUnpossessActorResT> res)
    {
        if (!res || !res->success)
        {
            JAMNET_LOG_WARN_LOC("Unpossess failed\n");
            return;
        }

        JAMNET_LOG_DEBUG("Stopped controlling actor\n");
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

        if (m_physics->IsReplayCandidate(params.desc.prefab))
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

    void ClientNetWorld::DespawnActorImpl(const NetId netId)
    {
        const auto e = GetEntity(netId);

        if (e == entt::null || !m_world.valid(e))
            return;

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

        if (m_world.ctx().contains<ClientPhysicsSystem>())
            m_world.ctx().get<ClientPhysicsSystem>().DespawnActor(e);

        m_world.destroy(e);
    }
}
