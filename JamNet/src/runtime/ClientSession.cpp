#include "pch.h"
#include "jamnet/runtime/ClientSession.h"

#include "jamnet/core/net/RPCAPI.h"
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

	void ClientTcpSession::HandleCustomPacket(const PacketHeaderView& view)
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

		flatbuffers::FlatBufferBuilder fbb(64);
		const auto root = fb::CreatefbTcpBindReq(fbb, m_userId);
		fbb.Finish(root);

		RPCCallOptions opt{ eChannel::TCP_DEFAULT, 3_s };
		RPCCallAsyncMember<fb::fbTcpBindReq, fb::fbTcpBindRes>(this, fbb.GetBufferPointer(), static_cast<uint32>(fbb.GetSize()), opt, this, &ClientTcpSession::OnTcpBindResponse);
	}

	void ClientTcpSession::OnTcpBindResponse(std::optional<RPCTableRef<fb::fbTcpBindRes>> res)
	{
		if (!res.has_value())
		{
			JAMNET_LOG_ERROR_LOC("TCP Bind RPC timeout or connection lost");
			return;
		}
		if (!(*res)->success() || m_userId != (*res)->user_id())
		{
			JAMNET_LOG_ERROR_LOC("TCP Bind RPC failed on server");
			return;
		}

		if (m_manager) m_manager->NotifyTcpBound();
	}






	void ClientUdpSession::OnConnected()
	{
		JAMNET_LOG_INFO("[Client #{}, UserId = {}] ClientUdpSession connected", m_userId - 1000, m_userId);

		if (m_manager && m_manager->IsTcpBound())
			RequestUdpBind();
	}

	void ClientUdpSession::OnDisconnected()
	{
		JAMNET_LOG_INFO("[Client #{}, UserId = {}] ClientUdpSession disconnected", m_userId - 1000, m_userId);
	}

	void ClientUdpSession::HandleCustomPacket(const PacketHeaderView& view)
	{
		if (!m_manager)
			return;

		if (view.Id() == CustomPacketId::WORLD_ASSIGNMENT)
		{
			flatbuffers::Verifier verifier(view.Payload(), view.PayloadSize());
			auto* table = flatbuffers::GetRoot<fb::fbRequestWorldAssignmentRes>(view.Payload());
			if (!table || !table->Verify(verifier))
				return;

			m_worldId = table->world_id();
			m_manager->NotifyWorldRequestResult(table->status(), table->request_action(), table->assignment_action(), table->reason(), m_worldId);
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

		flatbuffers::FlatBufferBuilder fbb(64);
		const auto root = fb::CreatefbUdpBindReq(fbb, m_userId);
		fbb.Finish(root);

		RPCCallOptions opt{ eChannel::RELIABLE_ORDERED, 3_s };

		RPCCallAsyncMember<fb::fbUdpBindReq, fb::fbUdpBindRes>(this, fbb.GetBufferPointer(), static_cast<uint32>(fbb.GetSize()), opt, this, &ClientUdpSession::OnUdpBindResponse);
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

	void ClientUdpSession::OnUdpBindResponse(std::optional<RPCTableRef<fb::fbUdpBindRes>> res)
	{
		if (!res.has_value())
		{
			JAMNET_LOG_ERROR_LOC("UDP Bind RPC timeout or connection lost");
			return;
		}
		if (!(*res)->success())
		{
			JAMNET_LOG_ERROR_LOC("UDP Bind RPC failed on server");
			return;
		}
		if (m_userId != (*res)->user_id())
		{
			JAMNET_LOG_ERROR_LOC("UDP Bind RPC userId mismatch");
			return;
		}

		if (m_manager) m_manager->NotifyUdpBound();
	}


	void ClientUdpSession::OnRequestWorldAssignmentRes(std::optional<RPCTableRef<fb::fbRequestWorldAssignmentRes>> res)
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

		m_worldId = (*res)->world_id();

		if (m_manager)
			m_manager->NotifyWorldRequestResult((*res)->status(), (*res)->request_action(), (*res)->assignment_action(), (*res)->reason(), m_worldId);
	}

	void ClientUdpSession::RequestWorldAction(fb::fbWorldRequestAction action, const WorldKey& targetWorld)
	{
		flatbuffers::FlatBufferBuilder fbb(128);
		const auto root = fb::CreatefbRequestWorldAssignmentReq(
			fbb,
			action,
			INVALID_WORLD_ID,
			static_cast<uint8>(targetWorld.kind),
			targetWorld.templateId,
			targetWorld.instanceId);
		fbb.Finish(root);

		RPCCallOptions opt{ eChannel::RELIABLE_ORDERED, 10_s };
		m_pendingWorldAction = static_cast<uint8>(action);

		RPCCallAsyncMember<fb::fbRequestWorldAssignmentReq, fb::fbRequestWorldAssignmentRes>(this, fbb.GetBufferPointer(), static_cast<uint32>(fbb.GetSize()), opt, this, &ClientUdpSession::OnRequestWorldAssignmentRes);
	}
}
