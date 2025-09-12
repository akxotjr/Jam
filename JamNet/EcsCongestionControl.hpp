#pragma once
#include "pch.h"

namespace jam::net::ecs
{
    struct CompCongestion
    {
        static constexpr uint32 MTU = 1024;
        uint32 cwnd = 4 * MTU;
        uint32 ssthresh = 32 * MTU;
        bool   fastRecovery = false;
        uint32 minCwnd = 1 * MTU;
        uint32 maxCwnd = 256 * MTU; // 필요 시 조정
    };

    struct EvCCRecvAck   { entt::entity e; };
    struct EvCCPacketLoss{ entt::entity e; };
    struct EvCCNewAck    { entt::entity e; };
    struct EvCCFastRTX   { entt::entity e; };

    struct CongestionHandlers
    {
        entt::registry* R{};

        void OnRecvAck(const EvCCRecvAck& ev)
        {
            auto& cc = R->get<CompCongestion>(ev.e);
            if (cc.fastRecovery)
            {
                cc.cwnd += CompCongestion::MTU; // 빠른 회복 중엔 ACK마다 1MSS
            }
            else if (cc.cwnd < cc.ssthresh)
            {
                // Slow Start: ACK마다 1MSS → RTT 당 cwnd 2배
                cc.cwnd += CompCongestion::MTU;
            }
            else
            {
                // Congestion Avoidance: RTT 당 1MSS ≈ ACK당 MSS*MSS/cwnd
                uint64 inc = (uint64)CompCongestion::MTU * (uint64)CompCongestion::MTU / std::max<uint32>(cc.cwnd, 1);
                if (inc == 0) inc = 1;      // 저속 구간 최소 증가 보장
                cc.cwnd += (uint32)inc;
            }
            if (cc.cwnd < cc.minCwnd) cc.cwnd = cc.minCwnd;
            if (cc.cwnd > cc.maxCwnd) cc.cwnd = cc.maxCwnd;
        }

        void OnPacketLoss(const EvCCPacketLoss& ev)
        {
            auto& cc = R->get<CompCongestion>(ev.e);
            cc.ssthresh = max(cc.cwnd / 2, cc.minCwnd);
            cc.cwnd = CompCongestion::MTU;
            cc.fastRecovery = false;
        }

        void OnNewAck(const EvCCNewAck& ev)
        {
            auto& cc = R->get<CompCongestion>(ev.e);
            if (cc.fastRecovery)
            {
                // 빠른 회복 종료
                cc.cwnd = cc.ssthresh;
                cc.fastRecovery = false;
            }
        }

        void OnFastRTX(const EvCCFastRTX& ev)
        {
            auto& cc = R->get<CompCongestion>(ev.e);
            cc.ssthresh = max(cc.cwnd / 2, cc.minCwnd);
            // TCP Reno: ssthresh + 3MSS
            cc.cwnd = cc.ssthresh + 3 * CompCongestion::MTU;
            if (cc.cwnd > cc.maxCwnd) cc.cwnd = cc.maxCwnd;
            cc.fastRecovery = true;
        }
    };

    struct CongestionSinks
    {
        bool                    wired = false;
        entt::scoped_connection onRecvAck;
        entt::scoped_connection onPktLoss;
        entt::scoped_connection onFastRTX;
        entt::scoped_connection onNewACK;
        CongestionHandlers      handlers;
    };

    inline void CongestionControlWiringSystem(utils::exec::ShardLocal& L, uint64, uint64)
    {
        auto& R = L.world; auto& D = L.events;
        auto& s = R.ctx().emplace<CongestionSinks>();
        if (s.wired) return;
        s.handlers.R = &R;

        s.onRecvAck = D.sink<EvCCRecvAck>().connect<&CongestionHandlers::OnRecvAck>(&s.handlers);
        s.onPktLoss = D.sink<EvCCPacketLoss>().connect<&CongestionHandlers::OnPacketLoss>(&s.handlers);
        s.onNewACK  = D.sink<EvCCNewAck>().connect<&CongestionHandlers::OnNewAck>(&s.handlers);
        s.onFastRTX = D.sink<EvCCFastRTX>().connect<&CongestionHandlers::OnFastRTX>(&s.handlers);

        s.wired = true;
    }
}
