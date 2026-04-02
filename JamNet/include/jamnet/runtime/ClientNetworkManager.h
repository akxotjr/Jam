#pragma once

#include <jampx/IPhysicsFacade.h>

namespace jam::net
{
    class ClientTcpSession;
	class ClientUdpSession;
    class ClientTransportAdapter;
    class ClientNetWorld;

    struct ClientConfig
    {
        NetAddress  serverTcpAddress{ "127.0.0.1", 7777 };
        NetAddress  serverUdpAddress{ "127.0.0.1", 8888 };

        using PhysicsFactory = std::function<std::unique_ptr<px::IPhysicsFacade>()>;
        PhysicsFactory physicsFactory = nullptr;

        std::string levelPath;
    };

    class ClientNetworkManager
    {
        friend class ClientTcpSession;
        friend class ClientUdpSession;

    public:
        explicit ClientNetworkManager(const ClientConfig& config, uint64 userId);
        ~ClientNetworkManager();

        bool                                Connect();
        void                                Disconnect();
        bool                                IsConnected() const;
        bool                                IsTcpConnected() const;
        bool                                IsUdpConnected() const;

        ClientNetWorld*                     GetWorld() { return m_world.get(); }
        const ClientNetWorld*               GetWorld() const { return m_world.get(); }

        std::shared_ptr<ClientTcpSession>   GetTcpSession() const { return m_tcp; }
        std::shared_ptr<ClientUdpSession>   GetUdpSession() const { return m_udp; }

        void                                SetUserId(uint64 userId);
        uint64                              GetUserId() const { return m_userId; }

        void                                TryMatchmaking();              // bind 완료 이후 호출 가능
        bool                                IsWorldRunning() const { return m_worldRunning.load(std::memory_order_acquire); }
        uint32                              GetGroupId() const { return m_groupId.load(std::memory_order_relaxed); }

    protected:
        bool                                StartClientService();
        void                                StopClientService();

        bool                                ConnectTcp();
        bool                                ConnectUdp();

        void                                InitializeWorldSkeleton() const;
        void                                StartWorld(uint32 groupId);

        // bind 완료 통지 (sessions -> manager)
        void                                NotifyTcpBound();
        void                                NotifyUdpBound();

        // groupId 결과 통지 (udp session -> manager)
        void                                NotifyMatchmakingSuccess(uint32 groupId);

    private:
        void                                UpdateSessionReadyState();

    private:
        ClientConfig                             m_config;

        std::shared_ptr<ClientService>           m_service;
        std::shared_ptr<ClientTcpSession>        m_tcp;
        std::shared_ptr<ClientUdpSession>        m_udp;
        std::shared_ptr<ClientTransportAdapter>  m_transportAdapter;
        std::shared_ptr<ClientNetWorld>          m_world;

        uint64                                   m_userId{ 0 };
        std::atomic_bool                         m_running{ false };

        std::atomic_bool                        m_tcpBound{ false };
        std::atomic_bool                        m_udpBound{ false };
        std::atomic_bool                        m_worldRunning{ false };
        std::atomic_bool                        m_matchmakingInFlight{ false };

        std::atomic<uint32>                     m_groupId{ 0 };
    };
}
