#include "pch.h"
#include "jamnet/runtime/ClientSession.h"
#include "jamnet/runtime/ClientNetworkManager.h"

#include "jamnet/sync/networld/ClientNetWorld.h"
#include "jamnet/sync/transport/CustomPacketHelper.h"

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

    void ClientTcpSession::OnTcpBindResponse(std::optional<fb::fbTcpBindResT> res)
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

        if (view.Id() == CustomPacketId::WORLD_ASSIGNMENT)
        {
            flatbuffers::Verifier verifier(view.Payload(), view.PayloadSize());
            auto* table = flatbuffers::GetRoot<fb::fbRequestWorldAssignmentRes>(view.Payload());
            if (!table || !table->Verify(verifier))
                return;

            fb::fbRequestWorldAssignmentResT res{};
            table->UnPackTo(&res);
            m_worldId = res.world_id;
            m_manager->NotifyWorldRequestResult(res.status, res.request_action, res.assignment_action, res.reason, m_worldId);
            return;
        }

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

    void ClientUdpSession::RequestAutoAssignWorld()
    {
		RequestWorldAction(fb::fbWorldRequestAction_AutoAssign);
    }

    void ClientUdpSession::RequestJoinWorld(const WorldKey& targetWorld)
    {
		RequestWorldAction(fb::fbWorldRequestAction_Join, targetWorld);
    }

	void ClientUdpSession::RequestLeaveWorld()
	{
		RequestWorldAction(fb::fbWorldRequestAction_Leave);
	}

	void ClientUdpSession::RequestTransferWorld(const WorldKey& targetWorld)
	{
		RequestWorldAction(fb::fbWorldRequestAction_Transfer, targetWorld);
	}

    void ClientUdpSession::OnUdpBindResponse(std::optional<fb::fbUdpBindResT> res)
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


    void ClientUdpSession::OnRequestWorldAssignmentRes(std::optional<fb::fbRequestWorldAssignmentResT> res)
    {
        if (!res.has_value())
        {
            JAMNET_LOG_ERROR("Request world assignment RPC: timeout or connection lost");
            if (m_manager)
                m_manager->NotifyWorldRequestResult(
                    static_cast<uint8>(eWorldAssignmentStatus::Failed),
                    m_pendingWorldAction,
                    static_cast<uint8>(eWorldAssignmentAction::None),
                    static_cast<uint8>(eWorldTransferReason::Timeout),
                    INVALID_WORLD_ID);
            return;
        }

        m_worldId = res->world_id;

        if (m_manager)
			m_manager->NotifyWorldRequestResult(res->status, res->request_action, res->assignment_action, res->reason, m_worldId);
    }

	void ClientUdpSession::RequestWorldAction(fb::fbWorldRequestAction action, const WorldKey& targetWorld)
	{
		fb::fbRequestWorldAssignmentReqT req{};
		req.action                   = action;
		req.target_world_kind        = static_cast<uint8>(targetWorld.kind);
		req.target_world_template_id = targetWorld.templateId;
		req.target_world_instance_id = targetWorld.instanceId;

		RPCCallOptions opt{ eChannelType::RELIABLE_ORDERED, 10_s };
		m_pendingWorldAction = static_cast<uint8>(action);

		auto self = static_pointer_cast<ClientUdpSession>(shared_from_this());
		RPCCallAsyncMember<fb::fbRequestWorldAssignmentReqT, fb::fbRequestWorldAssignmentResT>(self, std::move(req), opt, this, &ClientUdpSession::OnRequestWorldAssignmentRes);
	}
}
