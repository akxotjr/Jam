#include "pch.h"
#include "jamnet/runtime/ClientSession.h"
#include "jamnet/runtime/ClientNetworkManager.h"

#include "jamnet/sync/networld/ClientNetWorld.h"

namespace jam::net
{



    void ClientTcpSession::OnConnected()
    {
        JAMNET_LOG_INFO("[Client #{}, UserId = {}] ClientTcpSession connected", m_userId - 1000, m_userId);

        RequestTcpBind();
    }

    void ClientTcpSession::OnDisconnected()
    {
        JAMNET_LOG_INFO("[Client #{}, UserId = {}] ClientTcpSession disconnected", m_userId - 1000, m_userId);
    }

    void ClientTcpSession::HandleCustomPacket(const PacketView& view)
    {
        if (!m_manager)
            return;

        if (auto* world = m_manager->GetWorld())
        {
            world->OnRecvPacket(view);
        }
    }

    void ClientTcpSession::RequestTcpBind()
    {
		if (m_userId == 0)
        {
            JAMNET_LOG_ERROR_LOC("UserId is not set before TCP bind");
            return;
        }

        fb::fbTcpBindReqT req{};
        req.user_id = m_userId;

        RPCCallOptions opt{ eChannelType::TCP_DEFAULT, 3_s };
        auto self = static_pointer_cast<ClientTcpSession>(shared_from_this());
        RPCCallAsyncMember<fb::fbTcpBindReqT, fb::fbTcpBindResT>(self, std::move(req), opt, this, &ClientTcpSession::OnTcpBindResponse);
    }

    void ClientTcpSession::OnTcpBindResponse(optional<fb::fbTcpBindResT> res)
    {
        if (!res.has_value())
        {
            JAMNET_LOG_ERROR_LOC("TCP Bind RPC timeout or connection lost");
            return;
        }
        if (!res->success || m_userId != res->user_id)
        {
            JAMNET_LOG_ERROR_LOC("TCP Bind RPC failed on server");
            return;
        }

        if (m_manager) m_manager->NotifyTcpBound();
    }






    void ClientUdpSession::OnConnected()
    {
        JAMNET_LOG_INFO("[Client #{}, UserId = {}] ClientUdpSession connected", m_userId - 1000, m_userId);

        RequestUdpBind();
    }

    void ClientUdpSession::OnDisconnected()
    {
        JAMNET_LOG_INFO("[Client #{}, UserId = {}] ClientUdpSession disconnected", m_userId - 1000, m_userId);
    }

    void ClientUdpSession::HandleCustomPacket(const PacketView& view)
    {
        if (!m_manager)
            return;

        if (auto* world = m_manager->GetWorld())
        {
            world->OnRecvPacket(view);
        }
    }

    void ClientUdpSession::RequestUdpBind()
    {
        if (m_userId == 0)
        {
            JAMNET_LOG_ERROR_LOC("UserId is not set before UDP bind");
            return;
        }

        fb::fbUdpBindReqT req{};
        req.user_id = m_userId;

        RPCCallOptions opt{ eChannelType::RELIABLE_ORDERED, 3_s };

        auto self = static_pointer_cast<ClientUdpSession>(shared_from_this());
		RPCCallAsyncMember<fb::fbUdpBindReqT, fb::fbUdpBindResT>(self, std::move(req), opt, this, &ClientUdpSession::OnUdpBindResponse);
    }

    void ClientUdpSession::RequestGroupId()
    {
        fb::fbRequestGroupIdReqT req{};
        
        RPCCallOptions opt{ eChannelType::RELIABLE_ORDERED, 3_s };

        auto self = static_pointer_cast<ClientUdpSession>(shared_from_this());
        RPCCallAsyncMember<fb::fbRequestGroupIdReqT, fb::fbRequestGroupIdResT>(self, std::move(req), opt, this, &ClientUdpSession::OnRequestGroupIdRes);
    }

    void ClientUdpSession::OnUdpBindResponse(optional<fb::fbUdpBindResT> res)
    {
        if (!res.has_value())
        {
            JAMNET_LOG_ERROR_LOC("UDP Bind RPC timeout or connection lost");
            return;
        }
        if (!res->success)
        {
            JAMNET_LOG_ERROR_LOC("UDP Bind RPC failed on server");
            return;
        }
		if (m_userId != res->user_id)
        {
            JAMNET_LOG_ERROR_LOC("UDP Bind RPC userId mismatch");
            return;
        }

        if (m_manager) m_manager->NotifyUdpBound();
    }


    void ClientUdpSession::OnRequestGroupIdRes(optional<fb::fbRequestGroupIdResT> res)
    {
        if (!res.has_value())
        {
            JAMNET_LOG_ERROR("Request GroupID RPC: timeout or connection lost");
            return;
        }

        m_groupId = res->group_id;

        if (m_manager) m_manager->NotifyMatchmakingSuccess(m_groupId);
    }
}
