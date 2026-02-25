#include "pch.h"

#include "jamnet/sync/networld/ClientNetWorld.h"

#include "jamnet/sync/transport/CustomPacketHelper.h"

#include "jamnet/sync/replication/NetActorComponents.h"
#include "jamnet/sync/replication/NetWorldContext.h"

#include "jamnet/sync/replication/ClientInputSystem.h"
#include "jamnet/sync/replication/ClientPhysicsSystem.h"
#include "jamnet/sync/replication/ClientReplicationSystem.h"
#include "jamnet/sync/replication/ClientSamplingSystem.h"
#include "jamnet/sync/replication/ReplicationUtils.h"
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

        if (!m_levelPath.empty())
            m_physics->LoadLevel(m_levelPath);

        InitClientNetWorldCtx(m_world);

        m_world.ctx().emplace<ClientNetWorld*>(this);
        m_world.ctx().emplace<TickCounter>().Init();
        m_world.ctx().emplace<ClientInputSystem>(m_world).Init();
		m_world.ctx().emplace<ClientReplicationSystem>(m_world).Init();
        m_world.ctx().emplace<ClientPhysicsSystem>(m_world, m_physics.get()).Init();
        m_world.ctx().emplace<ClientSamplingSystem>(m_world).Init();
	}

    void ClientNetWorld::SetTransportSystem(shared_ptr<ITransportEndpoint> transport)
    {
		m_transport = std::move(transport);
    }

    void ClientNetWorld::SetPhysicsFacade(unique_ptr<px::IPhysicsFacade> physics)
    {
        m_physics = std::move(physics);
    }

    entt::entity ClientNetWorld::GetEntity(uint32 netId)
    {
        if (auto it = m_netIdToEntity.find(netId); it != m_netIdToEntity.end())
        {
            return it->second;
        }
        return entt::null;
    }


    void ClientNetWorld::Send(const shared_ptr<SendBuffer>& buf)
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
    }

    void ClientNetWorld::SpawnActor(const SpawnParams& params)
    {
        Post(Job([this, params]()
            {
                SpawnActorImpl(params);
            }));
    }

    void ClientNetWorld::DespawnActor(uint32 netId)
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
        if (!m_transport || !params.desc.IsValid())
            return;

        fb::fbSpawnActorReqT req{};

        req.spawn_req_id         = params.spawnId;
        req.prefab_key           = params.desc.prefab.value;
        req.owner_user_id        = params.owned ? m_userId : 0;
		req.controller_user_id   = params.controlled ? m_userId : 0;

        if (params.desc.cs.has_value())
        {
            PackedCharacterFull160 packed{};
            if (!PackCharacterFull160(params.desc.cs.value(), packed))
                return;

            req.initial_char_state = make_unique<fb::fbCharacterFull160>(packed.data0, packed.data1, packed.data2);
        }
        else if (params.desc.rs.has_value())
        {
            PackedRigidFull192 packed{};
            if (!PackRigidFull192(params.desc.rs.value(), packed))
                return;

            req.initial_rigid_state = make_unique<fb::fbTransformFull>(packed.data0, packed.data1, packed.data2);
        }

        if (params.desc.teamId.has_value()) req.team_id = params.desc.teamId.value();
        if (params.desc.partId.has_value()) req.part_id = params.desc.partId.value();

		RPCCallOptions opt{ .channel = eChannelType::RELIABLE_ORDERED, .timeout_ns = 10_s };
        m_transport->RPCCallAwaitMember<fb::fbSpawnActorReqT, fb::fbSpawnActorResT>(m_userId, eProtocolType::UDP, std::move(req), opt, this, &ClientNetWorld::OnSpawnActorResponse);
    }

    void ClientNetWorld::RequestDespawnActor(uint32 netId)
    {
        if (!m_transport)
            return;

        fb::fbDespawnActorReqT req{};
        req.net_id = netId;

        RPCCallOptions opt{ .channel = eChannelType::RELIABLE_ORDERED, .timeout_ns = 1_s };
        m_transport->RPCCallAwaitMember<fb::fbDespawnActorReqT, fb::fbDespawnActorResT>(m_userId, eProtocolType::UDP, std::move(req), opt, this, &ClientNetWorld::OnDespawnActorResponse);
    }

    void ClientNetWorld::RequestPossessActor(uint32 netId)
    {
		if (!m_transport) return;

        fb::fbPossessActorReqT req{};
		req.net_id = netId;
        RPCCallOptions opt{ eChannelType::RELIABLE_ORDERED, 2_s };
		m_transport->RPCCallAwaitMember<fb::fbPossessActorReqT, fb::fbPossessActorResT>(m_userId, eProtocolType::UDP, std::move(req), opt, this, &ClientNetWorld::OnPossessActorResponse);
    }

    void ClientNetWorld::RequestUnpossessActor(uint32 netId)
    {
        if (!m_transport) return;

		fb::fbUnpossessActorReqT req{};
		req.net_id = netId;
        RPCCallOptions opt{ eChannelType::RELIABLE_ORDERED, 1_s };
		m_transport->RPCCallAwaitMember<fb::fbUnpossessActorReqT, fb::fbUnpossessActorResT>(m_userId, eProtocolType::UDP, std::move(req), opt, this, &ClientNetWorld::OnUnpossesActorResponse);
    }




    entt::entity ClientNetWorld::EnsureReplicatedActor(uint32 netId, px::PrefabKey prefabKey, uint64 owner, uint64 controller)
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

        m_world.emplace<NetIdentity>(e, NetIdentity{ netId });
        m_world.emplace<NetPrefabKey>(e, NetPrefabKey{ prefabKey });
        m_world.emplace<OwnershipTag>(e, OwnershipTag{ owner });
        m_world.emplace<ControlTag>(e, ControlTag{ controller });

        const auto kind = m_physics->GetKind(prefabKey);
        m_world.emplace<NetActorBodyKind>(e, NetActorBodyKind{ kind });

        m_netIdToEntity.emplace(netId, e);

        RenderActorSpawnedEvent event{};
        event.userId    = m_userId;
        event.isLocal   = false;
        event.key       = MakeObjectKey(e);

        GLOBAL_EVENTBUS_PUBLISH(event);

        return e;
    }

    entt::entity ClientNetWorld::TryConfirmPendingSpawn(uint32 spawnReqId, uint32 netId)
    {
        if (spawnReqId == 0 || netId == 0)
            return entt::null;

        auto it = m_spawnReqIdToEntity.find(spawnReqId);
        if (it == m_spawnReqIdToEntity.end())
            return entt::null;

        entt::entity e = it->second;
        m_spawnReqIdToEntity.erase(it);

        if (e == entt::null || !m_world.valid(e))
            return entt::null;

        m_world.emplace_or_replace<NetIdentity>(e, NetIdentity{ netId });

        if (m_world.all_of<NetPendingSpawnTag>(e)) m_world.remove<NetPendingSpawnTag>(e);
        if (m_world.all_of<NetSpawnRequestId>(e))  m_world.remove<NetSpawnRequestId>(e);

        m_netIdToEntity[netId] = e;

        const uint64 owner      = m_world.get<OwnershipTag>(e).userId;
        const uint64 controller = m_world.get<ControlTag>(e).userId;

        bool isLocal = (owner == controller) && (controller == m_userId);
        if (isLocal) m_localNetId = netId;

        RenderActorSpawnedEvent event{};
        event.userId        = m_userId;
        event.spawnReqId    = spawnReqId;
        event.key           = MakeObjectKey(e);
        event.isLocal       = isLocal;
        GLOBAL_EVENTBUS_PUBLISH(event);

        return e;
    }



    void ClientNetWorld::TickOnShard()
	{
        if (!m_world.ctx().contains<TickCounter>()
            || !m_world.ctx().contains<ClientInputSystem>()
            || !m_world.ctx().contains<ClientReplicationSystem>()
            || !m_world.ctx().contains<ClientPhysicsSystem>()
            || !m_world.ctx().contains<ClientSamplingSystem>())
            return;

        const uint64 before = NOW_NS();

        m_world.ctx().get<TickCounter>().Tick();
        m_world.ctx().get<ClientInputSystem>().Tick();
        m_world.ctx().get<ClientReplicationSystem>().Tick();
        m_world.ctx().get<ClientPhysicsSystem>().Tick();
        m_world.ctx().get<ClientSamplingSystem>().Tick();
	
        const uint64 after = NOW_NS();

        JAMNET_LOG_DEBUG("1 tick elapsed time(ns) : {}", after - before);
	}

    void ClientNetWorld::ProcessSnapshot(const PacketView& view)
    {
        flatbuffers::Verifier verifier(view.Payload(), view.PayloadSize());
        if (!fb::VerifyfbSnapshotBuffer(verifier))
            return;

        auto fbSnap = fb::UnPackfbSnapshot(view.Payload());
        if (!fbSnap) return;

        auto snap = std::make_shared<fb::fbSnapshotT>(std::move(*fbSnap));

        Post(Job([this, snap]()
            {
				if (auto* repl = m_world.ctx().find<ClientReplicationSystem>())
				{
                    repl->EnqueueSnapshot(std::move(*snap));
				}
            }));
    }




    void ClientNetWorld::OnSpawnActorResponse(optional<fb::fbSpawnActorResT> res)
    {
        if (!res)
        {
            JAMNET_LOG_WARN_LOC("Spawn RPC timeout or connection lost\n");
            return;
        }

        const uint64 spawnReqId = res->spawn_req_id;

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

        TryConfirmPendingSpawn(spawnReqId, res->net_id);
    }

    void ClientNetWorld::OnDespawnActorResponse(optional<fb::fbDespawnActorResT> res)
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

    void ClientNetWorld::OnPossessActorResponse(optional<fb::fbPossessActorResT> res)
    {
		if (!res.has_value() || !res.value().success)
        {
            JAMNET_LOG_WARN_LOC("Possess RPC timeout or connection lost\n");
            return;
        }

		auto view = m_world.view<NetIdentity>();
        for (auto e : view)
        {
            if (view.get<NetIdentity>(e).netId == res->net_id)
            {
                m_world.emplace_or_replace<ControlTag>(e, ControlTag{ m_userId });
                JAMNET_LOG_DEBUG("Now controlling actor NetID=%llu\n", res->net_id);
                break;
            }
        }
    }

    void ClientNetWorld::OnUnpossesActorResponse(optional<fb::fbUnpossessActorResT> res)
    {
        if (!res || !res->success)
        {
            JAMNET_LOG_WARN_LOC("Unpossess failed\n");
            return;
        }

        JAMNET_LOG_DEBUG("Stopped controlling actor\n");
    }

    void ClientNetWorld::SpawnActorImpl(const SpawnParams& params)
    {
        entt::entity e = m_world.create();

        m_world.emplace<NetPendingSpawnTag>(e);
        m_world.emplace<NetSpawnRequestId>(e, NetSpawnRequestId{ params.spawnId });
        m_world.emplace<NetPrefabKey>(e, NetPrefabKey{ params.desc.prefab });
        m_world.emplace<OwnershipTag>(e, OwnershipTag{ params.owned ? m_userId : 0 });
        m_world.emplace<ControlTag>(e, ControlTag{ params.controlled ? m_userId : 0 });

        m_spawnReqIdToEntity.emplace(params.spawnId, e);

        RequestSpawnActor(params);
    }

    void ClientNetWorld::DespawnActorImpl(const uint32 netId)
    {
        const auto e = GetEntity(netId);

        if (e == entt::null || !m_world.valid(e))
            return;

        if (const auto* req = m_world.try_get<NetSpawnRequestId>(e))
        {
            if (auto it = m_spawnReqIdToEntity.find(req->requestId); it != m_spawnReqIdToEntity.end() && it->second == e)
                m_spawnReqIdToEntity.erase(it);
        }

        if (const auto* id = m_world.try_get<NetIdentity>(e))
        {
            if (auto it = m_netIdToEntity.find(id->netId); it != m_netIdToEntity.end() && it->second == e)
                m_netIdToEntity.erase(it);
        }

        if (m_world.ctx().contains<ClientPhysicsSystem>())
            m_world.ctx().get<ClientPhysicsSystem>().DespawnActor(e);

        m_world.destroy(e);
    }
}
