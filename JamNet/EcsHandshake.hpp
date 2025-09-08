#pragma once

#include "pch.h"
#include "HandshakeManager.h"

namespace jam::net::ecs
{
	struct CompEndpoint;
	// Components

	struct CompHandshake
	{
		eHandshakeState     state = eHandshakeState::DISCONNECTED;
		uint64              lastHsTime = 0;
		uint32              retryCount = 0;
		uint64              timeWaitStart = 0;
	};


	// Events

	struct EvHsCmdConnect
	{
		entt::entity e{ entt::null };
	};

	struct EvHsCmdDisconnect
	{
		entt::entity e{ entt::null };
	};

	struct EvHsPacket
	{
		entt::entity e;
		eSystemPacketId id;
	};

	// Handlers

    struct HandshakeHandlers
	{
        entt::registry* R{};


        void Send(entt::entity e, eSystemPacketId id)
    	{
            auto& ep = R->get<CompEndpoint>(e);
            auto buf = PacketBuilder::CreateHandshakePacket(id);
            ep.owner->SendDirect(buf);
        }

        void OnCmdConnect(const EvHsCmdConnect& ev) {
            auto& hs = R->get<CompHandshake>(ev.e);
            if (hs.state != eHandshakeState::DISCONNECTED && hs.state != eHandshakeState::CONNECT_SYN_SENT) return;
            Send(ev.e, eSystemPacketId::CONNECT_SYN);
            if (hs.state == eHandshakeState::DISCONNECTED) hs.state = eHandshakeState::CONNECT_SYN_SENT;
            hs.lastHsTime = Clock::Instance().GetCurrentTick();
        }

        void OnPacket(const EvHsPacket& ev) {
            auto& hs = R->get<CompHandshake>(ev.e);
            switch (ev.id) {
            case eSystemPacketId::CONNECT_SYN:
                if (hs.state != eHandshakeState::DISCONNECTED) return;
                hs.state = eHandshakeState::CONNECT_SYN_RECEIVED;
                Send(ev.e, eSystemPacketId::CONNECT_SYNACK);
                hs.state = eHandshakeState::CONNECT_SYNACK_SENT;
                hs.lastHsTime = Clock::Instance().GetCurrentTick();
                break;
            case eSystemPacketId::CONNECT_SYNACK:
                if (hs.state != eHandshakeState::CONNECT_SYN_SENT) return;
                hs.state = eHandshakeState::CONNECT_SYNACK_RECEIVED;
                Send(ev.e, eSystemPacketId::CONNECT_ACK);
                hs.state = eHandshakeState::CONNECTED;
                hs.lastHsTime = Clock::Instance().GetCurrentTick();
                // ep.owner->OnLinkEstablished(); // 필요 시 이벤트/콜백으로
                break;
            case eSystemPacketId::CONNECT_ACK:
                if (hs.state != eHandshakeState::CONNECT_SYNACK_SENT) return;
                hs.state = eHandshakeState::CONNECTED;
                // ep.owner->OnLinkEstablished();
                break;

            case eSystemPacketId::DISCONNECT_FIN:
            {
                switch (hs.state) {
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
                hs.lastHsTime = Clock::Instance().GetCurrentTick();
            } break;

            case eSystemPacketId::DISCONNECT_FINACK:
            {
                switch (hs.state) {
                case eHandshakeState::DISCONNECT_FIN_SENT:
                    hs.state = eHandshakeState::DISCONNECT_FINACK_RECEIVED;
                    Send(ev.e, eSystemPacketId::DISCONNECT_ACK);
                    hs.state = eHandshakeState::TIME_WAIT;
                    hs.timeWaitStart = Clock::Instance().GetCurrentTick();
                    break;
                case eHandshakeState::CLOSING:
                    Send(ev.e, eSystemPacketId::DISCONNECT_ACK);
                    hs.state = eHandshakeState::TIME_WAIT;
                    hs.timeWaitStart = Clock::Instance().GetCurrentTick();
                    break;
                default: break;
                }
                hs.lastHsTime = Clock::Instance().GetCurrentTick();
            } break;

            case eSystemPacketId::DISCONNECT_ACK:
                if (hs.state == eHandshakeState::DISCONNECT_FINACK_SENT) {
                    hs.state = eHandshakeState::DISCONNECTED;
                    // ep.owner->OnLinkTerminated();
                }
                else if (hs.state == eHandshakeState::TIME_WAIT) {
                    // 중복 ACK 무시
                }
                break;

            default: break;
            }
        }

        void OnCmdDisconnect(const EvHsCmdDisconnect& ev) {
            auto& hs = R->get<CompHandshake>(ev.e);
            if (hs.state != eHandshakeState::CONNECTED && hs.state != eHandshakeState::DISCONNECT_FIN_SENT) return;
            Send(ev.e, eSystemPacketId::DISCONNECT_FIN);
            if (hs.state == eHandshakeState::CONNECTED) hs.state = eHandshakeState::DISCONNECT_FIN_SENT;
            hs.lastHsTime = Clock::Instance().GetCurrentTick();
        }
    };


	// Sinks

    struct HandshakeSinks
	{
        bool wired = false;
        entt::scoped_connection a, b, c;
        HandshakeHandlers handlers;
    };


	// Systems

    inline void HandshakeWiringSystem(utils::exec::ShardLocal& L, uint64, uint64)
	{
        auto& R = L.world; auto& D = L.events;
        auto& sinks = R.ctx().emplace<HandshakeSinks>();
    	if (sinks.wired) return;
    	sinks.handlers.R = &R;

        sinks.a = D.sink<EvHsCmdConnect>().connect<&HandshakeHandlers::OnCmdConnect>(&sinks.handlers);
        sinks.b = D.sink<EvHsPacket>().connect<&HandshakeHandlers::OnPacket>(&sinks.handlers);
        sinks.c = D.sink<EvHsCmdDisconnect>().connect<&HandshakeHandlers::OnCmdDisconnect>(&sinks.handlers);

        sinks.wired = true;
    }

    inline void HandshakeTickSystem(utils::exec::ShardLocal& L, uint64, uint64) {
        auto& R = L.world;
        uint64 now = utils::Clock::Instance().GetCurrentTick();
        auto view = R.view<CompHandshake, CompEndpoint>();
        for (auto e : view) {
            auto& hs = view.get<CompHandshake>(e);
            switch (hs.state) {
            case eHandshakeState::TIME_WAIT:
                if (now - hs.timeWaitStart >= 2 * 30000 /*MSL*2*/) {
                    hs.state = eHandshakeState::DISCONNECTED;
                    // ep.owner->OnLinkTerminated();
                }
                break;
            case eHandshakeState::CONNECT_SYN_SENT:
            case eHandshakeState::CONNECT_SYNACK_SENT:
            case eHandshakeState::DISCONNECT_FIN_SENT:
            case eHandshakeState::DISCONNECT_FINACK_SENT:
            case eHandshakeState::CLOSING:
                if (hs.lastHsTime != 0 && (now - hs.lastHsTime) >= 5000 /*TIMEOUT*/) {
                    if (hs.retryCount >= 5) { hs.state = eHandshakeState::TIME_OUT; /*terminate*/ }
                    else {
                        ++hs.retryCount;
                        // 현재 상태에 맞는 재전송
                        if (hs.state == eHandshakeState::CONNECT_SYN_SENT)       HandshakeHandlers{ &R }.Send(e, eSystemPacketId::CONNECT_SYN);
                        else if (hs.state == eHandshakeState::CONNECT_SYNACK_SENT)HandshakeHandlers{ &R }.Send(e, eSystemPacketId::CONNECT_SYNACK);
                        else if (hs.state == eHandshakeState::DISCONNECT_FIN_SENT)HandshakeHandlers{ &R }.Send(e, eSystemPacketId::DISCONNECT_FIN);
                        else if (hs.state == eHandshakeState::DISCONNECT_FINACK_SENT || hs.state == eHandshakeState::CLOSING)
                            HandshakeHandlers{ &R }.Send(e, eSystemPacketId::DISCONNECT_FINACK);
                        hs.lastHsTime = now;
                    }
                }
                break;
            default: break;
            }
        }
    }
}
