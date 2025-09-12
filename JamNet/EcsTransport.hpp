#pragma once

#include "pch.h"

#include "EcsTransportAPI.hpp"

#include "EcsReliability.hpp"
#include "EcsCongestionControl.hpp"    
#include "EcsNetstat.hpp"

#include "PacketBuilder.h"

namespace jam::net::ecs
{
	struct CompReliability;

	// Components

 //   enum class eTxReason : uint8
	//{
 //       NORMAL,
 //       RETRANSMIT,
 //       CONTROL,       // Handshake, FIN, ACK(���� ����)
 //       ACK_ONLY,       // ���� ACK
 //   };

    struct PendingTx
	{
        Sptr<SendBuffer>    buf;
        eTxReason           reason{ eTxReason::NORMAL };
        uint32              size = 0;
    };

    struct CompTransportTx
    {
        xvector<PendingTx>  queue;
        uint64              lastFlush_ns = 0_ns;
        uint64              bytesQueued = 0;
        bool                flushRequested = false;
    };

    // Events

    struct EvTxEnqueue
	{
        entt::entity        e{ entt::null };
        Sptr<SendBuffer>    buf;
        eTxReason           reason{ eTxReason::NORMAL };
        uint32              size = 0;
    };

    struct EvTxFlush
	{
        entt::entity        e{ entt::null };
    };

    // ���� ��� (���� config �̵�)
    constexpr uint32 TRANSPORT_BATCH_MAX            = 32;
    constexpr uint64 TRANSPORT_FLUSH_INTERVAL_NS    = 1'000'000_ns;       // 1ms
    constexpr uint64 TRANSPORT_FORCE_INTERVAL_NS    = 5'000'000_ns;       // 5ms (�ִ� ����)
    constexpr uint64 TRANSPORT_IMMEDIATE_CTRL_NS    = 0_ns;               // Control ���

    struct TransportHandlers
    {
        entt::registry* R{};

        static int32 Priority(eTxReason r) noexcept
    	{
            switch (r)
        	{
            case eTxReason::CONTROL:    return 0;
            case eTxReason::RETRANSMIT: return 0;
            case eTxReason::ACK_ONLY:   return 1;
            case eTxReason::NORMAL:     return 2;
            default: return 3;
            }
        }


        // todo: change to PacketAnalysis
        static bool IsReliable(const Sptr<SendBuffer>&buf)
    	{
            if (!buf || !buf->Buffer()) return false;
            auto* h = reinterpret_cast<PacketHeader*>(buf->Buffer());
            return h->IsReliable();
        }


        // Piggyback �õ� (���� �� true)
        bool TryPiggyback(entt::entity e, PendingTx& txp)
    	{
            if (!R->all_of<CompReliability>(e)) return false;
            auto& cr = R->get<CompReliability>(e);
            if (!cr.hasPendingAck) return false;
            if (txp.reason == eTxReason::ACK_ONLY) return false;
            if (!IsReliable(txp.buf)) return false;

            auto* ph = reinterpret_cast<PacketHeader*>(txp.buf->Buffer());
            uint16 curSize = ph->GetSize();
            uint32 alloc = txp.buf->AllocSize();
            if (alloc - curSize < sizeof(AckHeader)) return false;

            auto* ack = reinterpret_cast<AckHeader*>(txp.buf->Buffer() + curSize);
            ack->latestSeq = cr.pendingAckSeq;
            ack->bitfield = cr.pendingAckBitfield;

            const uint16 newSize = static_cast<uint16>(curSize + sizeof(AckHeader));
            ph->SetSize(newSize);
            ph->SetFlags(ph->GetFlags() | PacketFlags::PIGGYBACK_ACK);
            txp.buf->Close(newSize);

            cr.hasPendingAck = false;
            cr.pendingAckSeq = 0;
            cr.pendingAckBitfield = 0;
            cr.firstPendingAckTime_ns = 0_ns;

            return true;
        }


        bool CanFlush(entt::entity e)
        {
            // ȥ��/���ö���Ʈ �˻� (����)
            if (!R->all_of<CompReliability, CompCongestion>(e)) return true;
            auto& cr = R->get<CompReliability>(e);
            auto& cc = R->get<CompCongestion>(e);
            return (cr.inFlightSize < cc.cwnd);
        }

        void OnEnqueue(const EvTxEnqueue& ev)
        {
            if (!ev.buf || !ev.buf->Buffer()) return;
            auto& tx = R->get<CompTransportTx>(ev.e);
            tx.queue.push_back(PendingTx{ ev.buf, ev.reason, ev.size });
            tx.bytesQueued += ev.size;

            R->ctx().get<entt::dispatcher>().enqueue<EvNsOnSend>(EvNsOnSend{ ev.e, ev.size });

            // ��� �÷��� ����
            bool immediate = false;
            switch (ev.reason)
            {
            case eTxReason::CONTROL:
            case eTxReason::ACK_ONLY:
                immediate = true;
            	break;
            case eTxReason::RETRANSMIT:
                immediate = true;
            	break;
            default: break;
            }

            if (tx.queue.size() >= TRANSPORT_BATCH_MAX) immediate = true;
            if (immediate && CanFlush(ev.e))
            {
                R->ctx().get<entt::dispatcher>().enqueue<EvTxFlush>(EvTxFlush{ ev.e });
            }
        }

        void OnFlush(const EvTxFlush& ev)
        {
            if (!R->all_of<CompTransportTx, CompEndpoint>(ev.e)) return;
            auto& tx = R->get<CompTransportTx>(ev.e);
            if (tx.queue.empty()) return;

            // (1) ���� ACK Ÿ�Ӿƿ��̸� standalone ACK_ONLY ��Ŷ ���� (Piggyback �� ã�� ���)
            if (R->all_of<CompReliability>(ev.e)) 
            {
                auto& cr = R->get<CompReliability>(ev.e);
                if (cr.hasPendingAck) 
                {
                    uint64 now = utils::Clock::Instance().NowNs();
                    if ((now - cr.firstPendingAckTick) >= MAX_DELAY_TICK_PIGGYBACK_ACK) 
                    {
                        auto ackBuf = PacketBuilder::CreateReliabilityAckPacket(cr.pendingAckSeq, cr.pendingAckBitfield);
                        tx.queue.push_back(PendingTx{ ackBuf, eTxReason::ACK_ONLY, ackBuf->WriteSize() });
                    }
                }
            }

            // (2) �켱���� ����
            std::stable_sort(tx.queue.begin(), tx.queue.end(),
                [](const PendingTx& a, const PendingTx& b) {
                    return Priority(a.reason) < Priority(b.reason);
                });

            // (3) Piggyback 1ȸ �õ� (�ڿ������� Reliable �ĺ�)
            bool piggyOK = false;
            if (R->all_of<CompReliability>(ev.e)) {
                auto& cr = R->get<CompReliability>(ev.e);
                if (cr.hasPendingAck) {
                    for (int i = (int)tx.queue.size() - 1; i >= 0; --i) {
                        if (TryPiggyback(ev.e, tx.queue[i])) {
                            piggyOK = true;
                            break;
                        }
                    }
                }
            }

            // (4) �����ߴٸ� ACK_ONLY �׸� ����
            if (piggyOK) 
            {
                tx.queue.erase(std::remove_if(tx.queue.begin(), tx.queue.end(),
                    [](const PendingTx& p) { return p.reason == eTxReason::ACK_ONLY; }), tx.queue.end());
            }
            // (piggy ���� + timeout�̸� ACK_ONLY ���� �����Ƿ� �״�� ����)

            auto& ep = R->get<CompEndpoint>(ev.e);

            // ť �����Ͽ� ������ ���� �غ�
            xvector<PendingTx> batch;
            batch.swap(tx.queue);
            tx.bytesQueued = 0;
            tx.flushRequested = false;
            tx.lastFlush_ns = utils::Clock::Instance().NowNs();

            auto session = ep.owner; // UdpSession*
            if (!session) return;

            // IO-thread(PostCtrl)�� ����
            auto sending = std::make_shared<xvector<PendingTx>>(std::move(batch));
            session->PostCtrl(utils::job::Job([sending, session] {
	                for (auto& p : *sending)
	                {
	                    session->SendDirect(p.buf);
	                }
                }));
        }
    };

    // Sinks

    struct TransportSinks
    {
        bool                    wired = false;
        entt::scoped_connection onEnqueue;
        entt::scoped_connection onFlush;
        TransportHandlers       handlers;
    };

    // Systems

    inline void TransportWiringSystem(utils::exec::ShardLocal& L, uint64, uint64)
    {
        auto& R = L.world; auto& D = L.events;
        auto& s = R.ctx().emplace<TransportSinks>();
        if (s.wired) return;
        s.handlers.R = &R;

        s.onEnqueue     = D.sink<EvTxEnqueue>().connect<&TransportHandlers::OnEnqueue>(&s.handlers);
        s.onFlush       = D.sink<EvTxFlush>().connect<&TransportHandlers::OnFlush>(&s.handlers);

        s.wired = true;
    }

    inline void TransportTickSystem(utils::exec::ShardLocal& L, uint64 now_ns, uint64 dt_ns)
    {
        auto& R = L.world;
        auto view = R.view<CompTransportTx, CompEndpoint>();
        for (auto e : view)
        {
            auto& tx = view.get<CompTransportTx>(e);
            if (tx.queue.empty()) continue;

            const uint64 elapsed = now_ns - tx.lastFlush_ns;
            bool timeFlush = (elapsed >= TRANSPORT_FLUSH_INTERVAL_NS);
            bool forceFlush = (elapsed >= TRANSPORT_FORCE_INTERVAL_NS);

            // ȥ�� üũ
            bool canFlush = true;
            if (R.all_of<CompReliability, CompCongestion>(e))
            {
                auto& cr = R.get<CompReliability>(e);
                auto& cc = R.get<CompCongestion>(e);
                canFlush = (cr.inFlightSize < cc.cwnd) || forceFlush;
            }

            if ((timeFlush || forceFlush) && canFlush)
            {
                R.ctx().get<entt::dispatcher>().enqueue<EvTxFlush>(EvTxFlush{ e });
            }
        }
    }

    // Helper

    inline void EnqueueSend(entt::registry& R, entt::entity e, const Sptr<SendBuffer>& buf, eTxReason reason)
    {
        if (!R.all_of<CompTransportTx>(e)) 
            R.emplace<CompTransportTx>(e);
        uint32 sz = (buf && buf->Buffer()) ? buf->WriteSize() : 0;
        R.ctx().get<entt::dispatcher>().enqueue<EvTxEnqueue>(EvTxEnqueue{ e, buf, reason, sz });
    }
}
