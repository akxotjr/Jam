#include "pch.h"
#include "jamnet/runtime/ServerNetworkManager.h"


#include "jamnet/runtime/ServerSession.h"
#include "jamnet/runtime/ServerTransportAdapter.h"
#include "jamnet/runtime/matchmaking/DefaultMatchmaker.h"

#include "jamnet/sync/networld/ServerNetWorld.h"
#include "jamnet/sync/replication/ReplicationTypes.h"


namespace jam::net
{
    ServerNetworkManager::ServerNetworkManager(const ServerConfig& config)
        : m_config(config)
    {
        m_tranportAdapter   = std::make_shared<ServerTransportAdapter>();

        if (m_tranportAdapter)
            m_tranportAdapter->SetNetworkManager(this);

        SetMatchmaker(std::make_unique<DefaultMatchmaker>());
    }

    ServerNetworkManager::~ServerNetworkManager()
    {
        Stop();
    }

    bool ServerNetworkManager::Start()
    {
        if (m_running.load(std::memory_order_acquire))
            return true;

        if (!StartServerService())
            return false;

        m_running.store(true, std::memory_order_release);

        JAMNET_LOG_INFO("ServerNetworkManager started successfully");
        return true;
    }

    void ServerNetworkManager::Stop()
    {
        if (!m_running.exchange(false, std::memory_order_acq_rel))
            return;

	
        if (m_tranportAdapter)
            m_tranportAdapter.reset();

        {
            WRITE_LOCK
        	m_tcpSessions.clear();
            m_udpSessions.clear();
        }

        StopServerService();

        if (m_matchmaker)
        {
            m_matchmaker->Init(nullptr);
            m_matchmaker.reset();
        }

        JAMNET_LOG_INFO("ServerNetworkManager stopped");
    }

    void ServerNetworkManager::SetMatchmaker(unique_ptr<IMatchmaker> matchmaker)
    {
        if (m_matchmaker)
            m_matchmaker->Init(nullptr);

        m_matchmaker = std::move(matchmaker);

        if (m_matchmaker)
            m_matchmaker->Init(this);
    }


    ServerNetWorld* ServerNetworkManager::GetWorld(uint32 groupId)
    {
        READ_LOCK
        auto it = m_worlds.find(groupId);
        return (it != m_worlds.end()) ? it->second.get() : nullptr;
    }

    ServerNetWorld* ServerNetworkManager::GetOrCreateWorld(uint32 groupId)
    {
        if (groupId == 0)
            return nullptr;

        WRITE_LOCK
        auto& slot = m_worlds[groupId];
        if (!slot)
        {
            slot = std::make_shared<ServerNetWorld>();
            slot->SetGroupId(groupId);
            slot->SetTransportAdapter(m_tranportAdapter.get());

            if (m_config.physicsFactory)
            {
	            if (auto phys = m_config.physicsFactory())
                    slot->SetPhysicsFacade(std::move(phys));
            }

            slot->SetLevelPath(m_config.levelPath);

            slot->Init();
            slot->Tick(SIMULATION_TICK_NS);
        }

        return slot.get();
    }

    void ServerNetworkManager::DestroyWorld(uint32 groupId)
    {
        if (groupId == 0)
            return;

        shared_ptr<ServerNetWorld> victim;
        {
            WRITE_LOCK
        	auto it = m_worlds.find(groupId);
            if (it == m_worlds.end())
                return;

            victim = std::move(it->second);
            m_worlds.erase(it);
        }

        if (victim)
            victim->Stop();
    }

    void ServerNetworkManager::RegisterTcpSession(uint64 userId, const shared_ptr<ServerTcpSession>& tcp)
    {
        if (!userId || !tcp)
            return;

        WRITE_LOCK
    	m_tcpSessions[userId] = tcp;

        JAMNET_LOG_INFO("UserId = {}] TCP Session registered", userId);
    }

    void ServerNetworkManager::RegisterUdpSession(uint64 userId, const shared_ptr<ServerUdpSession>& udp)
    {
        if (!userId || !udp)
            return;

        WRITE_LOCK
    	m_udpSessions[userId] = udp;

        JAMNET_LOG_INFO("UserId = {}] UDP Session registered", userId);
    }

    void ServerNetworkManager::UnregisterSession(uint64 userId)
    {
        if (!userId)
            return;

        vector<uint32> groupsToNotify;
        {
            WRITE_LOCK
                m_tcpSessions.erase(userId);
            m_udpSessions.erase(userId);

            for (auto& [groupId, members] : m_groupMembers)
            {
                if (std::erase(members, userId) > 0)
                    groupsToNotify.push_back(groupId);
            }
        }

        for (uint32 groupId : groupsToNotify)
        {
            if (auto* world = GetWorld(groupId))
                world->Leave(userId);
        }

        JAMNET_LOG_INFO("UserId = {}] Session unregistered", userId);
    }

    shared_ptr<ServerTcpSession> ServerNetworkManager::FindTcpSession(uint64 userId)
    {
        READ_LOCK
    	auto it = m_tcpSessions.find(userId);
        return (it != m_tcpSessions.end()) ? it->second : nullptr;
    }

    shared_ptr<ServerUdpSession> ServerNetworkManager::FindUdpSession(uint64 userId)
    {
        READ_LOCK
    	auto it = m_udpSessions.find(userId);
        return (it != m_udpSessions.end()) ? it->second : nullptr;
    }

    void ServerNetworkManager::BroadcastPacket(const shared_ptr<SendBuffer>& buf, eProtocolType protocol)
    {
        if (!buf)
            return;

        READ_LOCK

        if (protocol == eProtocolType::TCP)
        {
            for (auto& session : m_tcpSessions | views::values)
            {
                if (session && session->IsConnected())
                {
                    session->Send(buf);
                }
            }
        }

        if (protocol == eProtocolType::UDP)
        {
            for (auto& session : m_udpSessions | views::values)
            {
                if (session && session->IsConnected())
                {
                    session->Send(buf);
                }
            }
        }
    }

    void ServerNetworkManager::SendToUser(uint64 userId, const shared_ptr<SendBuffer>& buf, eProtocolType protocol)
    {
        if (!buf)
            return;

        if (protocol == eProtocolType::TCP)
        {
            if (auto tcp = FindTcpSession(userId))
            {
                if (tcp->IsConnected())
                {
                    tcp->Send(buf);
                }
            }
        }

        if (protocol == eProtocolType::UDP)
        {
            if (auto udp = FindUdpSession(userId))
            {
                if (udp->IsConnected())
                {
                    udp->Send(buf);
                }
            }
        }
    }

    void ServerNetworkManager::JoinGroup(uint32 groupId, uint64 userId)
    {
        if (groupId == 0 || userId == 0)
            return;

        WRITE_LOCK
    	auto& members = m_groupMembers[groupId];
        if (ranges::find(members, userId) == members.end())
            members.push_back(userId);
    }

    void ServerNetworkManager::LeaveGroup(uint32 groupId, uint64 userId)
    {
        if (groupId == 0 || userId == 0)
            return;

        WRITE_LOCK
    	auto it = m_groupMembers.find(groupId);
        if (it == m_groupMembers.end())
            return;

        std::erase(it->second, userId);
        if (it->second.empty())
            m_groupMembers.erase(it);
    }

    void ServerNetworkManager::EnumerateConnectedUsers(const std::function<void(uint64)>& fn)
    {
        if (!fn) return;

        std::vector<uint64> users;
        {
            READ_LOCK
        	users.reserve(m_tcpSessions.size());
            for (const auto& uid : m_tcpSessions | views::keys)
                users.push_back(uid);
        }

        for (uint64 uid : users)
            fn(uid);
    }

    void ServerNetworkManager::EnumerateGroupUsers(uint32 groupId, const std::function<void(uint64)>& fn)
    {
        if (!fn || groupId == 0) return;

        std::vector<uint64> users;
        {
            READ_LOCK
        	auto it = m_groupMembers.find(groupId);
            if (it == m_groupMembers.end())
                return;

            users = it->second; // 복사해서 락 밖에서 순회
        }

        for (uint64 uid : users)
            fn(uid);
    }


    bool ServerNetworkManager::StartServerService()
    {
        ServiceConfig cfg{};
		cfg.localTcpAddress     = m_config.tcpAddress;
		cfg.localUdpAddress     = m_config.udpAddress;
		cfg.maxTcpSessionCount  = m_config.maxConnections;
		cfg.maxUdpSessionCount  = m_config.maxConnections;

        m_service = std::make_shared<ServerService>(cfg);
        if (!m_service)
            return false;

        m_service->SetSessionFactory<ServerTcpSession, ServerUdpSession>();
        m_service->SetSessionInitCallback([this](const shared_ptr<Session>& session)
            {
                if (auto tcp = dynamic_pointer_cast<ServerTcpSession>(session))
                {
                    tcp->SetNetworkManager(this);
                    RPCRegisterRequest<fb::fbTcpBindReqT>(tcp, tcp.get(), &ServerTcpSession::OnTcpBindRequest);
                }
                else if (auto udp = dynamic_pointer_cast<ServerUdpSession>(session))
                {
                    udp->SetNetworkManager(this);
					RPCRegisterRequest<fb::fbUdpBindReqT>(udp, udp.get(), &ServerUdpSession::OnUdpBindRequest);
                    // matchmaking
                        // (현재 ServerTcpSession에 OnRequestGroupIdReq가 있는데, UDP로 옮기는 게 자연스러움)
                    RPCRegisterRequest<fb::fbRequestGroupIdReqT>(udp, udp.get(), &ServerUdpSession::OnRequestGroupIdReq);

                    // world RPC는 m_world가 아니라 "udp session 라우터"에 등록
                    RPCRegisterRequest<fb::fbSpawnActorReqT>(udp, udp.get(), &ServerUdpSession::OnSpawnActorRequest);
                    RPCRegisterRequest<fb::fbDespawnActorReqT>(udp, udp.get(), &ServerUdpSession::OnDespawnActorRequest);
                    RPCRegisterRequest<fb::fbPossessActorReqT>(udp, udp.get(), &ServerUdpSession::OnPossessActorRequest);
                    RPCRegisterRequest<fb::fbUnpossessActorReqT>(udp, udp.get(), &ServerUdpSession::OnUnpossessActorRequest);
                }
            });

        m_service->Init();

        if (!m_service->Start())
            return false;

        return true;
    }

    void ServerNetworkManager::StopServerService()
    {
        if (m_service)
        {
            m_service->CloseService();
            m_service.reset();
        }
    }


}
