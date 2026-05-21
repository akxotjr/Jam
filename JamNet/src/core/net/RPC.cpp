
#include "pch.h"
#include "jamnet/core/net/RPC.h"
#include "jamnet/core/net/PacketBuilder.h"
#include "jamnet/core/net/SessionComponents.h"

namespace jam::net
{
	void RPC::EnsureRegistry(entt::registry& R)
	{
		if (!R.ctx().contains<RPCHandlersRegistry>())
		{
			R.ctx().emplace<RPCHandlersRegistry>();
		}
	}

	void RPC::HandleIncomingPacket(entt::registry& R, const entt::entity e, const PacketHeaderView& view, Packet packet)
	{
		if (view.Type() != ePacketType::RPC)
			return;

		if (view.Id() != E2U(eRpcPacketId::FLATBUFFER_RPC))
			return;

		BYTE*  payload = view.Payload();
		uint32 len     = view.payloadSize;

		if (HasFlag(view.Flags(), PacketFlags::PIGGYBACK_ACK) && view.IsReliable())
		{
			if (len < sizeof(ACK_DATA))
				return;
			len -= static_cast<uint32>(sizeof(ACK_DATA));
		}

		if (len < sizeof(RpcHeader))
			return;

		RpcHeader hdr{};
		std::memcpy(&hdr, payload, sizeof(RpcHeader));

		const uint16 rpcId     = hdr.rpcId;
		const uint32 requestId = hdr.requestId;
		const uint8  flags     = hdr.flags;

		const bool isRequest  = (flags & RpcFlags::REQUEST) != 0;
		const bool isResponse = (flags & RpcFlags::RESPONSE) != 0;
		if (isRequest == isResponse)
			return;

		const BYTE*  body    = payload + sizeof(RpcHeader);
		const uint32 bodyLen = len - static_cast<uint32>(sizeof(RpcHeader));

		// RESPONSE: RpcState inflight 우선 처리
		if (isResponse)
		{
			if (auto* rpcState = R.try_get<RpcState>(e))
			{
				if (auto st = rpcState->PopRequest(requestId))
				{
					if (st->onPayload) st->onPayload(body, bodyLen, std::move(packet));
					if (st->onDone)    st->onDone(true);
					return;
				}
			}

			if (R.ctx().contains<RPCHandlersRegistry>())
			{
				auto& reg = R.ctx().get<RPCHandlersRegistry>();

				EntityRPCKey key{ .entity = e, .rpcId = rpcId };
				auto it = reg.resHandlers.find(key);
				if (it != reg.resHandlers.end())
				{
					it->second(e, body, bodyLen, requestId);
					return;
				}

				auto gitRes = reg.globalResHandlers.find(rpcId);
				if (gitRes != reg.globalResHandlers.end())
				{
					gitRes->second(e, body, bodyLen, requestId);
					return;
				}
			}

			JAMNET_LOG_WARN("[RPC] Unhandled response. entity={}, rpcId={}, requestId={}, bodyLen={}",
				static_cast<uint32>(e), rpcId, requestId, bodyLen);
			return;
		}

		// REQUEST: 엔티티별 -> 글로벌 순
		if (R.ctx().contains<RPCHandlersRegistry>())
		{
			auto& reg = R.ctx().get<RPCHandlersRegistry>();

			EntityRPCKey key{ .entity = e, .rpcId = rpcId };
			auto it = reg.reqHandlers.find(key);
			if (it != reg.reqHandlers.end())
			{
				it->second(e, body, bodyLen, requestId);
				return;
			}

			auto gitReq = reg.globalReqHandlers.find(rpcId);
			if (gitReq != reg.globalReqHandlers.end())
			{
				gitReq->second(e, body, bodyLen, requestId);
				return;
			}

			JAMNET_LOG_WARN("[RPC] Unhandled request. entity={}, rpcId={}, requestId={}, bodyLen={}",
				static_cast<uint32>(e), rpcId, requestId, bodyLen);
			return;
		}

		JAMNET_LOG_WARN("[RPC] Missing handler registry. entity={}, rpcId={}, requestId={}, flags={}",
			static_cast<uint32>(e), rpcId, requestId, flags);
	}

	void RPCUnregisterAll(entt::registry& R, entt::entity e)
	{
		if (!R.ctx().contains<RPCHandlersRegistry>())
			return;

		auto& reg = R.ctx().get<RPCHandlersRegistry>();

		for (auto it = reg.reqHandlers.begin(); it != reg.reqHandlers.end();)
		{
			if (it->first.entity == e)
				it = reg.reqHandlers.erase(it);
			else
				++it;
		}

		for (auto it = reg.resHandlers.begin(); it != reg.resHandlers.end();)
		{
			if (it->first.entity == e)
				it = reg.resHandlers.erase(it);
			else
				++it;
		}
	}




}
