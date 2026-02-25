#include "pch.h"
#include "jamnet/core/net/RPC.h"
#include "jamnet/core/net/RecvBuffer.h"
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

    void RPC::HandleIncomingPacket(entt::registry& R, const entt::entity e, const PacketView& view, const std::shared_ptr<RecvBuffer>&)
    {
        if (view.Type() != ePacketType::RPC)
            return;

        BYTE* payload = view.Payload();
        uint32 len = view.payloadSize;
        if (len < sizeof(RpcHeader))
            return;

        RpcHeader hdr{};
        std::memcpy(&hdr, payload, sizeof(RpcHeader));

        uint16 rpcId = hdr.rpcId;
        uint32 requestId = hdr.requestId;
        uint8 flags = hdr.flags;

        // 임시 진단: piggyback ACK 의심 시 한 번 더 시도 (EcsRpc.hpp 로직 유지)
        const bool piggy = HasFlag(view.Flags(), PacketFlags::PIGGYBACK_ACK) && view.IsReliable();
        EntityRPCKey diagKey{ e, rpcId };
        if (piggy &&
            len >= sizeof(ACK_DATA) + sizeof(RpcHeader) &&
            R.ctx().contains<RPCHandlersRegistry>() &&
            R.ctx().get<RPCHandlersRegistry>().reqHandlers.find(diagKey) == R.ctx().get<RPCHandlersRegistry>().reqHandlers.end())
        {
            std::memcpy(&hdr, payload + sizeof(ACK_DATA), sizeof(RpcHeader));
            rpcId = hdr.rpcId;
            requestId = hdr.requestId;
            flags = hdr.flags;
        }

        const BYTE* body = payload + sizeof(RpcHeader);
        const uint32 bodyLen = len - static_cast<uint32>(sizeof(RpcHeader));

        // RESPONSE: RpcState inflight 우선 처리
        if ((flags & RpcFlags::RESPONSE) != 0)
        {
            if (auto* rpcState = R.try_get<RpcState>(e))
            {
                auto st = rpcState->PopRequest(requestId);
                if (st)
                {
                    if (st->onPayload) st->onPayload(body, bodyLen);
                    if (st->onDone)    st->onDone(true);
                    return;
                }
            }

            if (R.ctx().contains<RPCHandlersRegistry>())
            {
                auto& reg = R.ctx().get<RPCHandlersRegistry>();

                EntityRPCKey key{ e, rpcId };
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

            return;
        }

        // REQUEST: 엔티티별 → 글로벌 순
        if (R.ctx().contains<RPCHandlersRegistry>())
        {
            auto& reg = R.ctx().get<RPCHandlersRegistry>();

            EntityRPCKey key{ e, rpcId };
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
            }
        }
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
