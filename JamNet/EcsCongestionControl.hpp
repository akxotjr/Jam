#pragma once
#include "pch.h"

namespace jam::net::ecs
{
    // Component

    struct CompCongestion
	{
        uint32 cwnd = 4 * MTU;
        uint32 ssthresh = 32 * MTU;
        bool   fastRecovery = false;
        static constexpr uint32 MTU = 1024;
    };

    // Events

    struct EvCCRecvAck { entt::entity e; };
    struct EvCCPacketLoss { entt::entity e; };
    struct EvCCNewAck { entt::entity e; };
    struct EvCCFastRTX { entt::entity e; };

    // Handlers

    struct CongestionHandlers
    {
        entt::registry* R{};


        void OnRecvAck(const EvCCRecvAck& ev)
        {
            auto& cs = R->get<CompCongestion>(ev.e);
            if (cs.fastRecovery)        
                cs.cwnd += CompCongestion::MTU;
            else if (cs.cwnd < cs.ssthresh) 
                cs.cwnd += CompCongestion::MTU;
            else                         
                cs.cwnd += CompCongestion::MTU * (CompCongestion::MTU / cs.cwnd);
        }

        void OnPacketLoss(const EvCCPacketLoss& ev)
        {
            auto& cs = R->get<CompCongestion>(ev.e);
            cs.ssthresh = cs.cwnd / 2;
            cs.cwnd = CompCongestion::MTU;
            cs.fastRecovery = false;
        }

        void OnNewAck(const EvCCNewAck& ev)
        {
            auto& cs = R->get<CompCongestion>(ev.e);
            if (cs.fastRecovery)
            {
	            cs.cwnd = cs.ssthresh;
            	cs.fastRecovery = false;
            }
        }

        void OnFastRTX(const EvCCFastRTX& ev)
        {
            auto& cs = R->get<CompCongestion>(ev.e);
            cs.ssthresh = cs.cwnd / 2;
            cs.cwnd = cs.ssthresh + 3 * CompCongestion::MTU;
            cs.fastRecovery = true;
        }
    };

    // Sinks

    struct CongestionSinks
    {
        bool                    wired = false;

        entt::scoped_connection onRecvAck;
        entt::scoped_connection onPktLoss;
        entt::scoped_connection onFastRTX;
        entt::scoped_connection onNewACK;

        CongestionHandlers      handlers;
    };

    // Systems

    inline void CongestionControlWiringSystem(utils::exec::ShardLocal& L, uint64, uint64)
	{
        auto& R = L.world; auto& D = L.events;
        auto& s = R.ctx().emplace<CongestionSinks>();
        if (s.wired) return;
        s.handlers.R = &R;

        s.onRecvAck     = D.sink<EvCCRecvAck>().connect<&CongestionHandlers::OnRecvAck>(&s.handlers);
        s.onPktLoss     = D.sink<EvCCPacketLoss>().connect<&CongestionHandlers::OnPacketLoss>(&s.handlers);
        s.onNewACK      = D.sink<EvCCNewAck>().connect<&CongestionHandlers::OnNewAck>(&s.handlers);
        s.onFastRTX     = D.sink<EvCCFastRTX>().connect<&CongestionHandlers::OnFastRTX>(&s.handlers);

        s.wired = true;
	}
}
