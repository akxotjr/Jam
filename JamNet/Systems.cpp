#include "pch.h"
#include "Systems.h"
#include "Components.h"
#include "EcsEvents.h"
#include "NetStatManager.h"
#include "ReliableTransportManager.h"

namespace jam::net::ecs
{

#pragma region Reliability System

    // todo : store �����͸� auto �߷��� �� �ȵ�



    void ReliabilityWiringSystem(utils::exec::ShardLocal& L, uint64, uint64)
    {
        auto& R = L.world;
        auto& D = L.events;

        auto& sinks = R.ctx().emplace<ReliabilitySinks>();

        if (sinks.wired) return;

        sinks.handlers.R = &R; // �ڵ鷯�� ������Ʈ�� ����

        sinks.onAck = D.sink<EvRecvAck>().connect<&ReliabilityHandlers::OnRecvAck>(&sinks.handlers);
        sinks.onData = D.sink<EvRecvData>().connect<&ReliabilityHandlers::OnRecvData>(&sinks.handlers);
        sinks.onNack = D.sink<EvRecvNack>().connect<&ReliabilityHandlers::OnRecvNack>(&sinks.handlers);
        sinks.onDidSend = D.sink<EvDidSend>().connect<&ReliabilityHandlers::OnDidSend>(&sinks.handlers);
        sinks.onPiggyFail = D.sink<EvPiggybackFailed>().connect<&ReliabilityHandlers::OnPiggybackFailed>(&sinks.handlers);

        sinks.wired = true;
    }

    void ReliabilityTickSystem(utils::exec::ShardLocal& L, uint64 now_ns, uint64 dt_ns)
    {
        auto& R = L.world;
        auto& pools = R.ctx().get<EcsHandlePools>();
        const uint64 now = jam::utils::Clock::Instance().NowNs();

        auto view = R.view<ReliabilityState, CompEndpoint>();
        for (auto e : view) 
        {
            auto& rs = view.get<ReliabilityState>(e);
            auto& ep = view.get<CompEndpoint>(e);
            auto* st = pools.reliability.get(rs.hStore);
            if (!st) continue;

            if (st->pending.empty()) 
            {
                // ���� ACK ��� ���� �Ǵ�
                if (rs.hasPendingAck && (now - rs.firstPendingAckTick) >= MAX_DELAY_TICK_PIGGYBACK_ACK) 
                {
                    ep.owner->SendDirect(PacketBuilder::CreateReliabilityAckPacket(rs.pendingAckSeq, rs.pendingAckBitfield));
                    rs.hasPendingAck = false; rs.pendingAckSeq = 0; rs.pendingAckBitfield = 0; rs.firstPendingAckTick = 0;
                }
                continue;
            }

            xvector<uint16> toRemove;
            xvector<uint16> toRTX;

            for (auto& [seq, pk] : st->pending) 
            {
                const uint64 elapsed = now - pk.timestamp;
                if (elapsed >= RETRANSMIT_TIMEOUT || pk.retryCount >= MAX_RETRY_COUNT) 
                {
                    toRemove.push_back(seq);
                    ep.owner->GetNetStatManager()->OnPacketLoss();
                }
                else if (elapsed >= RETRANSMIT_INTERVAL) 
                {
                    toRTX.push_back(seq);
                }
            }

            for (auto seq : toRemove) 
            {
                if (auto it = st->pending.find(seq); it != st->pending.end()) 
                {
                    rs.inFlightSize -= it->second.size;
                    st->pending.erase(it);
                }
            }
            for (auto seq : toRTX) {
                if (auto it = st->pending.find(seq); it != st->pending.end()) 
                {
                    ep.owner->SendDirect(it->second.buffer);
                    it->second.retryCount++;
                    it->second.timestamp = now;
                    ep.owner->GetNetStatManager()->OnRTO();
                    ep.owner->GetCongestionController()->OnPacketLoss();
                }
            }
            // ����: Update()�� RTO/RTX ������ �״�� �ݿ�. :contentReference[oaicite:7]{index=7}

            // (�ɼ�) ����� �־ ���� ACK �Ӱ�� �÷���
            if (rs.hasPendingAck && (now - rs.firstPendingAckTick) >= MAX_DELAY_TICK_PIGGYBACK_ACK) 
            {
                ep.owner->SendDirect(PacketBuilder::CreateReliabilityAckPacket(rs.pendingAckSeq, rs.pendingAckBitfield));
                rs.hasPendingAck = false; rs.pendingAckSeq = 0; rs.pendingAckBitfield = 0; rs.firstPendingAckTick = 0;
            }
        }
    }



#pragma endregion

}
