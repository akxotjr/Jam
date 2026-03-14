#pragma once

#include "jamnet/runtime/matchmaking/IMatchmaker.h"

#include <jampx/IPhysicsFacade.h>

namespace jam::net
{
	class ServerTcpSession;
	class ServerUdpSession;
	class ServerTransportAdapter;
    class ServerNetWorld;


    struct ServerConfig
    {
        NetAddress  tcpAddress{"127.0.0.1", 7777};
        NetAddress  udpAddress{"127.0.0.1", 8888};
        uint32      maxConnections = 1000;


        using PhysicsFactory = std::function<std::unique_ptr<px::IPhysicsFacade>()>;
        PhysicsFactory physicsFactory = nullptr;

        string levelPath;
    };

    class ServerNetworkManager
    {
    public:
        explicit ServerNetworkManager(const ServerConfig& config);
        ~ServerNetworkManager();

        bool                            Start();
        void                            Stop();
        bool                            IsRunning() const { return m_running.load(std::memory_order_acquire); };

        void                            SetMatchmaker(unique_ptr<IMatchmaker> matchmaker);
        IMatchmaker*                    GetMatchmaker() const { return m_matchmaker.get(); }

        shared_ptr<ServerService>       GetService() const { return m_service; }

        ServerNetWorld*                 GetWorld(uint32 groupId);
        ServerNetWorld*                 GetOrCreateWorld(uint32 groupId);
        void                            DestroyWorld(uint32 groupId);

    	
    	void                            RegisterTcpSession(uint64 userId, const shared_ptr<ServerTcpSession>& tcp);
        void                            RegisterUdpSession(uint64 userId, const shared_ptr<ServerUdpSession>& udp);
        void                            UnregisterSession(uint64 userId);

        shared_ptr<ServerTcpSession>    FindTcpSession(uint64 userId);
        shared_ptr<ServerUdpSession>    FindUdpSession(uint64 userId);

        void                            BroadcastPacket(const shared_ptr<SendBuffer>& buf, eProtocolType protocol);
        void                            SendToUser(uint64 userId, const shared_ptr<SendBuffer>& buf, eProtocolType protocol);

        void							JoinGroup(uint32 groupId, uint64 userId);
        void							LeaveGroup(uint32 groupId, uint64 userId);

        void							EnumerateConnectedUsers(const std::function<void(uint64)>& fn);
        void							EnumerateGroupUsers(uint32 groupId, const std::function<void(uint64)>& fn);


    private:
        bool                            StartServerService();
        void                            StopServerService();

    private:
        USE_LOCK

    	ServerConfig                                        m_config;

        shared_ptr<ServerService>                           m_service;
        shared_ptr<ServerTransportAdapter>                  m_tranportAdapter;

        unique_ptr<IMatchmaker>                             m_matchmaker = nullptr;

        unordered_map<uint32, shared_ptr<ServerNetWorld>>   m_worlds;           // groupId -> NetWorld

        unordered_map<uint64, shared_ptr<ServerTcpSession>> m_tcpSessions;      // userId -> Session mapping
        unordered_map<uint64, shared_ptr<ServerUdpSession>> m_udpSessions;

        unordered_map<uint32, vector<uint64>>				m_groupMembers;     // groupId -> members(userId)

        atomic<bool>                                        m_running{ false };
    };
}
