#pragma once
#include "jamnet/runtime/schema/gen/binding_handshake_generated.h"
#include "jamnet/runtime/schema/gen/matchmaking_generated.h"

namespace jam::net
{
    class ClientNetworkManager;

    class ClientTcpSession : public TcpSession
    {
    protected:
        void                    OnConnected() override;
        void                    OnDisconnected() override;
        void                    OnSend(int32 len) override {}
        void                    OnRecv(BYTE* buffer, int32 len) override {}
        void                    HandleCustomPacket(const PacketView& view) override;

    public:
        void                    SetNetworkManager(ClientNetworkManager* manager) { m_manager = manager; }
        void					SetUserId(uint64 userId) { m_userId = userId; }
        uint64					GetUserId() const { return m_userId; }

    private:
        void                    RequestTcpBind();
        void                    OnTcpBindResponse(std::optional<fb::fbTcpBindResT> res);

    private:
        ClientNetworkManager*   m_manager = nullptr;
		uint64                  m_userId = 0;
    };

    class ClientUdpSession : public UdpSession
    {
    protected:
        void                    OnConnected() override;
        void                    OnDisconnected() override;
        void                    OnSend(int32 len) override {}
        void                    OnRecv(BYTE* buffer, int32 len) override {}
        void                    HandleCustomPacket(const PacketView& view) override;

    public:
        void                    SetNetworkManager(ClientNetworkManager* manager) { m_manager = manager; }
        void					SetUserId(uint64 userId) { m_userId = userId; }
        uint64					GetUserId() const { return m_userId; }

        void                    RequestGroupId();

    private:
        void                    RequestUdpBind();

        void                    OnUdpBindResponse(std::optional<fb::fbUdpBindResT> res);
        void                    OnRequestGroupIdRes(std::optional<fb::fbRequestGroupIdResT> res);

    private:
        ClientNetworkManager*   m_manager = nullptr;
        uint64                  m_userId = 0;
        uint32                  m_groupId = 0;
    };
}