#pragma once

#include "jamnet/core/net/Session.h"
#include "jamnet/core/net/NetAddress.h"
#include "jamnet/sync/replication/ReplicationEvents.h"
#include "jamnet/runtime/world/WorldAssignmentTypes.h"

#include <jampx/IPhysicsFacade.h>

#include <mutex>
#include <optional>


namespace jam::net
{
	class ClientService;
	class ClientTcpSession;
	class ClientUdpSession;
    class ClientTransportAdapter;
    class ClientNetWorld;

    struct ClientConfig
    {
        NetAddress      serverTcpAddress    = { "127.0.0.1", 7777 };
        NetAddress      serverUdpAddress    = { "127.0.0.1", 8888 };

        using PhysicsFactory = std::function<std::unique_ptr<px::IPhysicsFacade>()>;
        PhysicsFactory  physicsFactory      = nullptr;

        std::string     levelPath;
        bool            headlessWorld       = false;
    };

    class ClientNetworkManager
    {
        friend class ClientTcpSession;
        friend class ClientUdpSession;

    public:
        explicit ClientNetworkManager(const ClientConfig& config, uint64 userId);
        ~ClientNetworkManager();

        bool                                    Connect();
        void                                    Disconnect();
        bool                                    IsConnected() const;
        bool                                    IsTcpConnected() const;
        bool                                    IsUdpConnected() const;

        ClientNetWorld*                         GetWorld() { return m_world.get(); }
        const ClientNetWorld*                   GetWorld() const { return m_world.get(); }

        std::shared_ptr<ClientTcpSession>       GetTcpSession() const { return m_tcp; }
        std::shared_ptr<ClientUdpSession>       GetUdpSession() const { return m_udp; }

        void                                    SetUserId(uint64 userId);
        uint64                                  GetUserId() const { return m_userId; }

        bool                                    RequestAutoAssignWorld();
        bool                                    RequestJoinWorld(const WorldKey& targetWorld);
        bool                                    RequestLeaveWorld();
        bool                                    RequestTransferWorld(const WorldKey& targetWorld);

        bool                                    IsTcpBound() const { return m_tcpBound.load(std::memory_order_acquire); }
        bool                                    IsUdpBound() const { return m_udpBound.load(std::memory_order_acquire); }
        bool                                    IsSessionReady() const { return m_sessionReady.load(std::memory_order_acquire); }
        bool                                    IsWorldRequestInFlight() const { return m_worldAssignmentInFlight.load(std::memory_order_acquire); }
        bool                                    IsWorldRunning() const { return m_worldRunning.load(std::memory_order_acquire); }
        WorldId                                 GetWorldId() const { return m_worldId.load(std::memory_order_relaxed); }
        ClientBindState                         GetBindState() const;
        std::optional<WorldRequestResultEvent>  GetLastWorldRequestResult() const;

    protected:
        bool                                    StartClientService();
        void                                    StopClientService();

        bool                                    ConnectTcp();
        bool                                    ConnectUdp();

        void                                    InitializeWorldSkeleton();
        void                                    StartWorld(WorldId worldId);
        void                                    StopWorld();

        // bind 완료 통지 (sessions -> manager)
        void                                    NotifyTcpBound();
        void                                    NotifyUdpBound();

        // worldId 결과 통지 (udp session -> manager)
        void                                    NotifyWorldRequestResult(uint8 status, uint8 requestAction, uint8 assignmentAction, uint8 reason, WorldId worldId);

    private:
        void                                    ResetWorldInstance();
        bool                                    RequestWorldAction(const std::function<void(ClientUdpSession&)>& issueRequest);
        void                                    PublishBindStateChangedEvent() const;
        void                                    UpdateSessionReadyState();

    private:
        ClientConfig                             m_config                   = {};

        std::shared_ptr<ClientService>           m_service                  = nullptr;
        std::shared_ptr<ClientTcpSession>        m_tcp                      = nullptr;
        std::shared_ptr<ClientUdpSession>        m_udp                      = nullptr;
        std::shared_ptr<ClientTransportAdapter>  m_transportAdapter         = nullptr;
        std::shared_ptr<ClientNetWorld>          m_world                    = nullptr;

        uint64                                   m_userId                   = 0;
        std::atomic_bool                         m_running                  = false;

        std::atomic_bool                         m_tcpBound                 = false;
        std::atomic_bool                         m_udpBound                 = false;
        std::atomic_bool                         m_sessionReady             = false;
        std::atomic_bool                         m_worldRunning             = false;
        std::atomic_bool                         m_worldAssignmentInFlight  = false;
                                                  
        std::atomic<WorldId>                     m_worldId                  = INVALID_WORLD_ID;
        mutable std::mutex                       m_lastWorldRequestLock;
        std::optional<WorldRequestResultEvent>   m_lastWorldRequestResult;
    };
}
