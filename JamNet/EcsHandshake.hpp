#pragma once

#include "pch.h"

#include "EcsTransportAPI.hpp"


namespace jam::net::ecs
{
    // fwd
	struct CompEndpoint;

    //	정상 종료(능동 종료측) :
    //	CONNECTED->FIN_SENT->FINACK_RECEIVED->TIME_WAIT->DISCONNECTED
    
    //	정상 종료(수동 종료측) :
    //	CONNECTED->FIN_RECEIVED->FINACK_SENT->DISCONNECTED
    
    //	동시 종료 :
    //	CONNECTED->FIN_SENT->CLOSING->TIME_WAIT->DISCONNECTED


    enum class eHandshakeState : uint8
    {
        DISCONNECTED,

        CONNECT_SYN_SENT,
        CONNECT_SYN_RECEIVED,
        CONNECT_SYNACK_SENT,
        CONNECT_SYNACK_RECEIVED,
        CONNECTED,

        DISCONNECT_FIN_SENT,
        DISCONNECT_FIN_RECEIVED,
        DISCONNECT_FINACK_SENT,
        DISCONNECT_FINACK_RECEIVED,
        DISCONNECT_ACK_SENT,
        DISCONNECT_ACK_RECEIVED,

        TIME_WAIT,
        CLOSING,	// Simultaneous Close

        TIME_OUT,
        ERROR_STATE,

    };


	// Components

    constexpr uint64 HANDSHAKE_TIMEOUT_NS   = 5'000'000'000_ns;     // 5 sec
    constexpr uint64 HANDSHAKE_MSL_NS       = 30'000'000'000_ns;    // 30 sec
    constexpr uint32 HANDSHAKE_MAX_RETRY    = 5;

	struct CompHandshake
	{
		eHandshakeState     state = eHandshakeState::DISCONNECTED;
        uint64              lastHsTime_ns = 0_ns;
		uint32              retryCount = 0;
		uint64              timeWaitStart_ns = 0_ns;
	};


	// Events

	struct EvHsConnect
	{
		entt::entity        e{ entt::null };
	};

	struct EvHsDisconnect
	{
		entt::entity        e{ entt::null };
	};

	struct EvHsRecv   // todo
	{
		entt::entity        e{ entt::null };
		eSystemPacketId     id;
	};

	// Handlers

    struct HandshakeHandlers
	{
        entt::registry* R{};


        void Send(entt::entity e, eSystemPacketId id)
    	{
            auto& ep = R->get<CompEndpoint>(e);
            auto buf = PacketBuilder::CreateHandshakePacket(id);
            EnqueueSend(*R, e, buf, eTxReason::CONTROL);
        }

        void OnConnect(const EvHsConnect& ev)
    	{
            auto& hs = R->get<CompHandshake>(ev.e);
            if (hs.state != eHandshakeState::DISCONNECTED && hs.state != eHandshakeState::CONNECT_SYN_SENT) 
                return;

            Send(ev.e, eSystemPacketId::CONNECT_SYN);

            if (hs.state == eHandshakeState::DISCONNECTED) 
                hs.state = eHandshakeState::CONNECT_SYN_SENT;

            hs.lastHsTime_ns = utils::Clock::Instance().NowNs();
        }

        void OnRecv(const EvHsRecv& ev)
    	{
            auto& hs = R->get<CompHandshake>(ev.e);
            switch (ev.id)
        	{
            case eSystemPacketId::CONNECT_SYN:
                if (hs.state != eHandshakeState::DISCONNECTED) return;
                hs.state = eHandshakeState::CONNECT_SYN_RECEIVED;
                Send(ev.e, eSystemPacketId::CONNECT_SYNACK);
                hs.state = eHandshakeState::CONNECT_SYNACK_SENT;
                hs.lastHsTime_ns = utils::Clock::Instance().NowNs();
                break;
            case eSystemPacketId::CONNECT_SYNACK:
                if (hs.state != eHandshakeState::CONNECT_SYN_SENT) return;
                hs.state = eHandshakeState::CONNECT_SYNACK_RECEIVED;
                Send(ev.e, eSystemPacketId::CONNECT_ACK);
                hs.state = eHandshakeState::CONNECTED;
                hs.lastHsTime_ns = utils::Clock::Instance().NowNs();
                // ep.owner->OnLinkEstablished(); // 필요 시 이벤트/콜백으로  //todo
                break;
            case eSystemPacketId::CONNECT_ACK:
                if (hs.state != eHandshakeState::CONNECT_SYNACK_SENT) return;
                hs.state = eHandshakeState::CONNECTED;
                // ep.owner->OnLinkEstablished();
                break;

            case eSystemPacketId::DISCONNECT_FIN:
            {
                switch (hs.state)
                {
                case eHandshakeState::CONNECTED:
                    hs.state = eHandshakeState::DISCONNECT_FIN_RECEIVED;
                    Send(ev.e, eSystemPacketId::DISCONNECT_FINACK);
                    hs.state = eHandshakeState::DISCONNECT_FINACK_SENT;
                    break;
                case eHandshakeState::DISCONNECT_FIN_SENT:
                    hs.state = eHandshakeState::CLOSING;
                    Send(ev.e, eSystemPacketId::DISCONNECT_FINACK);
                    break;
                case eHandshakeState::TIME_WAIT:
                    Send(ev.e, eSystemPacketId::DISCONNECT_ACK);
                    break;
                default: break;
                }
                hs.lastHsTime_ns = utils::Clock::Instance().NowNs();
            } break;

            case eSystemPacketId::DISCONNECT_FINACK:
            {
                switch (hs.state)
            		{
                case eHandshakeState::DISCONNECT_FIN_SENT:
                    hs.state = eHandshakeState::DISCONNECT_FINACK_RECEIVED;
                    Send(ev.e, eSystemPacketId::DISCONNECT_ACK);
                    hs.state = eHandshakeState::TIME_WAIT;
                    hs.timeWaitStart_ns = utils::Clock::Instance().NowNs();
                    break;
                case eHandshakeState::CLOSING:
                    Send(ev.e, eSystemPacketId::DISCONNECT_ACK);
                    hs.state = eHandshakeState::TIME_WAIT;
                    hs.timeWaitStart_ns = utils::Clock::Instance().NowNs();
                    break;
                default: break;
                }
                hs.lastHsTime_ns = utils::Clock::Instance().NowNs();
            } break;

            case eSystemPacketId::DISCONNECT_ACK:
                if (hs.state == eHandshakeState::DISCONNECT_FINACK_SENT) 
                {
                    hs.state = eHandshakeState::DISCONNECTED;
                    // ep.owner->OnLinkTerminated();
                }
                else if (hs.state == eHandshakeState::TIME_WAIT) 
                {
                    // 중복 ACK 무시
                }
                break;

            default: break;
            }
        }

        void OnDisconnect(const EvHsDisconnect& ev)
    	{
            auto& hs = R->get<CompHandshake>(ev.e);
            if (hs.state != eHandshakeState::CONNECTED && hs.state != eHandshakeState::DISCONNECT_FIN_SENT) 
                return;

            Send(ev.e, eSystemPacketId::DISCONNECT_FIN);

            if (hs.state == eHandshakeState::CONNECTED) 
                hs.state = eHandshakeState::DISCONNECT_FIN_SENT;

            hs.lastHsTime_ns = utils::Clock::Instance().NowNs();
        }
    };


	// Sinks

    struct HandshakeSinks
	{
        bool                        wired = false;
        entt::scoped_connection     onConnect;
        entt::scoped_connection     onDisonnect;
        entt::scoped_connection     onRecv;
        HandshakeHandlers           handlers;
    };


	// Systems

    inline void HandshakeWiringSystem(utils::exec::ShardLocal& L, uint64, uint64)
	{
        auto& R = L.world; auto& D = L.events;
        auto& sinks = R.ctx().emplace<HandshakeSinks>();
    	if (sinks.wired) return;
    	sinks.handlers.R = &R;

        sinks.onConnect        = D.sink<EvHsConnect>().connect<&HandshakeHandlers::OnConnect>(&sinks.handlers);
        sinks.onDisonnect      = D.sink<EvHsDisconnect>().connect<&HandshakeHandlers::OnDisconnect>(&sinks.handlers);
    	sinks.onRecv           = D.sink<EvHsRecv>().connect<&HandshakeHandlers::OnRecv>(&sinks.handlers);

        sinks.wired = true;
    }

    inline void HandshakeTickSystem(utils::exec::ShardLocal& L, uint64, uint64)
	{
        auto& R = L.world;
        uint64 now_ns = utils::Clock::Instance().NowNs();
        auto view = R.view<CompHandshake, CompEndpoint>();
        for (auto e : view) {
            auto& hs = view.get<CompHandshake>(e);
            switch (hs.state) {
            case eHandshakeState::TIME_WAIT:
                if (now_ns - hs.timeWaitStart_ns >= 2 * HANDSHAKE_MSL_NS) 
                {
                    hs.state = eHandshakeState::DISCONNECTED;
                    // ep.owner->OnLinkTerminated();
                }
                break;
            case eHandshakeState::CONNECT_SYN_SENT:
            case eHandshakeState::CONNECT_SYNACK_SENT:
            case eHandshakeState::DISCONNECT_FIN_SENT:
            case eHandshakeState::DISCONNECT_FINACK_SENT:
            case eHandshakeState::CLOSING:
                if (hs.lastHsTime_ns != 0 && (now_ns - hs.lastHsTime_ns) >= HANDSHAKE_TIMEOUT_NS)
                {
                    if (hs.retryCount >= 5)
                    {
	                    hs.state = eHandshakeState::TIME_OUT; /*terminate*/
                    }
                    else 
                    {
                        ++hs.retryCount;
                        // 현재 상태에 맞는 재전송
                        if (hs.state == eHandshakeState::CONNECT_SYN_SENT)       HandshakeHandlers{ &R }.Send(e, eSystemPacketId::CONNECT_SYN);
                        else if (hs.state == eHandshakeState::CONNECT_SYNACK_SENT)HandshakeHandlers{ &R }.Send(e, eSystemPacketId::CONNECT_SYNACK);
                        else if (hs.state == eHandshakeState::DISCONNECT_FIN_SENT)HandshakeHandlers{ &R }.Send(e, eSystemPacketId::DISCONNECT_FIN);
                        else if (hs.state == eHandshakeState::DISCONNECT_FINACK_SENT || hs.state == eHandshakeState::CLOSING)
                            HandshakeHandlers{ &R }.Send(e, eSystemPacketId::DISCONNECT_FINACK);
                        hs.lastHsTime_ns = now_ns;
                    }
                }
                break;
            default: break;
            }
        }
    }
}
