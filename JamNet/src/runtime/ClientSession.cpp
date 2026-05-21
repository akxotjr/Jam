#include "pch.h"
#include "jamnet/runtime/ClientSession.h"


#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/core/net/RPCAPI.h"
#include "jamnet/runtime/ClientNetworkManager.h"
#include "jamnet/runtime/world/ClientWorldActionSystem.h"
#include "jamnet/sync/networld/ClientPhysicalWorld.h"

#include "jamnet/sync/transport/CustomPacketHelper.h"

namespace jam::net
{
	namespace
	{
		fb::fbWorldAction ToFb(eWorldAction action)
		{
			if (action == eWorldAction::AutoAssign)	return fb::fbWorldAction_AutoAssign;
			if (action == eWorldAction::Join)		return fb::fbWorldAction_Join;
			if (action == eWorldAction::Leave)		return fb::fbWorldAction_Leave;
			if (action == eWorldAction::Transfer)	return fb::fbWorldAction_Transfer;
			if (action == eWorldAction::Promote)	return fb::fbWorldAction_Promote;

			return fb::fbWorldAction_MAX;
		}

		WorldActionResult ParseWorldActionResult(const fb::fbWorldActionRes& table)
		{
			WorldActionResult result
			{
				.status    = static_cast<eWorldActionStatus>(table.status()),
				.reason    = static_cast<eWorldActionReason>(table.reason()),
				.action    = static_cast<eWorldAction>(table.request_action()),
				.execFlags = WorldActionExecFlags(table.exec_flags()),
				.source    = WorldKey{ table.src_desc_id(), table.src_world_id() },
				.target    = WorldKey{ table.target_desc_id(), table.target_world_id() },
			};
			if (const auto* deltas = table.membership_deltas())
			{
				result.membershipDeltas.reserve(deltas->size());
				for (const fb::fbWorldMembershipDelta* delta : *deltas)
				{
					if (!delta)
						continue;

					result.membershipDeltas.push_back(WorldMembershipDelta
						{
							.op = delta->op() == fb::fbWorldMembershipDeltaOp_Remove
								? eWorldMembershipDeltaOp::Remove
								: eWorldMembershipDeltaOp::Upsert,
							.membership =
							{
								.key		  = WorldKey{ delta->key_desc_id(), delta->key_world_id() },
								.localWorldId = kInvalidLocalWorldId, // Resolved locally from NetWorldId.
								.kind		  = static_cast<eWorldKind>(delta->kind()),
								.role		  = static_cast<eWorldRole>(delta->role()),
								.presence	  = static_cast<eWorldMembershipPresence>(delta->presence()),
							},
						});
					}
			}
			if (const auto* worldRuntimeDeltas = table.world_runtime_deltas())
			{
				result.worldRuntimeDeltas.reserve(worldRuntimeDeltas->size());
				for (const fb::fbWorldRuntimeDelta* delta : *worldRuntimeDeltas)
				{
					if (!delta)
						continue;

					result.worldRuntimeDeltas.push_back(WorldRuntimeDelta
						{
							.key     = WorldKey{ delta->key_desc_id(), delta->key_world_id() },
							.runtime = static_cast<ePhysicalWorldRuntimeState>(delta->runtime()),
						});
				}
			}
			return result;
		}


	} // anonymous namespace



	void ClientTcpSession::OnLinkEstablished()
	{
		JAMNET_LOG_INFO("[AccountId = {}, UserId = {}] ClientTcpSession established. ip: {} | port: {}",
			GetAccountId(),
			GetUserId(),
			GetRemoteNetAddress().GetIpAddress(),
			GetRemoteNetAddress().GetPort());

		auto& L		= CurrentShardLocalChecked();
		auto& state = GetOrCreateUserShardState(L);
		auto* ctx	= state.EnsureUserContext(m_accountId);
		if (!ctx) return;

		ctx->userId = GetUserId();
		ctx->tcp    = GetSessionId();

		if (m_manager)
			m_manager->NotifyTcpBound(GetUserId());
	}

	void ClientTcpSession::OnDisconnected()
	{
		JAMNET_LOG_INFO("[AccountId = {}, UserId = {}] ClientTcpSession disconnected", GetAccountId(), GetUserId());
	}

	void ClientTcpSession::HandleCustomPacket(Packet packet)
	{
		auto view = PacketHeaderView::Parse(packet->Head(), packet->Size());
		const NetWorldId worldId = ResolveScopedPacketWorldId(view);
		if (worldId == kInvalidNetWorldId)
			return;

		if (auto* worldActionSystem = m_manager ? m_manager->GetWorldActionSystem() : nullptr)
			worldActionSystem->DispatchWorldPacket(worldId, std::move(packet));
	}





	void ClientUdpSession::OnLinkEstablished()
	{
		JAMNET_LOG_INFO("[AccountId = {}, UserId = {}] ClientUdpSession established. ip: {} | port: {}",
			GetAccountId(),
			GetUserId(),
			GetRemoteNetAddress().GetIpAddress(),
			GetRemoteNetAddress().GetPort());

		auto& L     = CurrentShardLocalChecked();
		auto& state = GetOrCreateUserShardState(L);
		auto* ctx   = state.FindUserContext(m_userId);
		if (!ctx) return;
		ctx->udp = GetSessionId();

		if (m_manager)
			m_manager->NotifyUdpBound(GetUserId());
	}

	void ClientUdpSession::OnDisconnected()
	{
		JAMNET_LOG_INFO("[AccountId = {}, UserId = {}] ClientUdpSession disconnected", GetAccountId(), GetUserId());
	}

	void ClientUdpSession::HandleCustomPacket(Packet packet)
	{
		if (!m_manager)
			return;

		auto view = PacketHeaderView::Parse(packet->Head(), packet->Size());
		if (!view.IsValid())
			return;

		if (view.Id() == CustomPacketId::WORLD_ASSIGNMENT)
		{
			flatbuffers::Verifier verifier(view.Payload(), view.PayloadSize());
			auto* table = flatbuffers::GetRoot<fb::fbWorldActionRes>(view.Payload());
			if (!table || !table->Verify(verifier))
				return;

			if (auto* worldActionSystem = m_manager->GetWorldActionSystem())
				worldActionSystem->ApplyWorldActionResult(ParseWorldActionResult(*table));
			return;
		}

		const NetWorldId worldId = ResolveScopedPacketWorldId(view);
		if (worldId == kInvalidNetWorldId)
		{
			if (view.Id() == CustomPacketId::SNAPSHOT || view.Id() == CustomPacketId::LIFECYCLE)
			{
				JAMNET_LOG_WARN(
					"[ClientUdpSession] Invalid scoped world id. account={} user={} packetId={} payloadSize={}",
					GetAccountId(),
					GetUserId(),
					static_cast<uint32>(view.Id()),
					view.PayloadSize());
			}
			return;
		}

		if (auto* worldActionSystem = m_manager->GetWorldActionSystem())
			worldActionSystem->DispatchWorldPacket(worldId, std::move(packet));
	}

	void ClientUdpSession::RequestWorldAction(const WorldActionRequest& req)
	{
		if (!m_manager)
			return;

		const fb::fbWorldAction fbAction = ToFb(req.action);
		if (fbAction == fb::fbWorldAction_MAX) return;

		flatbuffers::FlatBufferBuilder fbb(128);

		const auto root = fb::CreatefbWorldActionReq(
			fbb,
			fbAction,
			req.source.descId,
			req.source.worldId,
			req.target.descId,
			req.target.worldId);
		fbb.Finish(root);

		RPCCallOptions opt{ eChannel::RELIABLE_ORDERED, 10_s };

		RPCCallAsyncMember<fb::fbWorldActionReq, fb::fbWorldActionRes>(this, fbb.GetBufferPointer(), fbb.GetSize(), opt, this, &ClientUdpSession::OnWorldActionResponse);
	}

	void ClientUdpSession::OnWorldActionResponse(std::optional<RPCTableRef<fb::fbWorldActionRes>> res)
	{
		if (!res.has_value())
		{
			JAMNET_LOG_ERROR("Request world assignment RPC: timeout or connection lost");
			return;
		}

		if (!m_manager)
			return;

		auto* worldActionSystem = m_manager->GetWorldActionSystem();
		if (!worldActionSystem)
			return;

		WorldActionResult result = ParseWorldActionResult(**res);
		if (result.Succeeded() && result.action == eWorldAction::Leave)
			worldActionSystem->ApplyWorldActionResult(result);
	}
}
