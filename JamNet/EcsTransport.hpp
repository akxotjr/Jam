#pragma once

#include "pch.h"

#include "EcsTransportAPI.hpp"

#include "EcsReliability.hpp"
#include "EcsCongestionControl.hpp"    
#include "EcsFragment.hpp"
#include "EcsNetstat.hpp"

#include "PacketBuilder.h"
#include "ShardTLS.h"

namespace jam::net::ecs
{
	struct EvFgFragmentize;
}

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


     //   // todo: change to PacketAnalysis
     //   static bool IsReliable(const Sptr<SendBuffer>&buf)
    	//{
     //       if (!buf || !buf->Buffer()) return false;
     //       auto* h = reinterpret_cast<PacketHeader*>(buf->Buffer());
     //       return h->IsReliable();
     //   }


        // Piggyback �õ� (���� �� true)
        bool TryPiggyback(entt::entity e, PendingTx& txp)
    	{
            auto& L = SHARD_LOCAL_CHECKED();
            auto& R = L.world;
            auto& D = L.events;

            if (!R.all_of<CompReliability>(e)) return false;

            auto& cr = R.get<CompReliability>(e);
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
            auto& L = SHARD_LOCAL_CHECKED();
            auto& R = L.world;

            if (!R.all_of<CompReliability, CompCongestion>(e)) return true;
            auto& cr = R.get<CompReliability>(e);
            auto& cc = R.get<CompCongestion>(e);
            return (cr.inFlightSize < cc.cwnd);
        }

        void OnEnqueue(const EvTxEnqueue& ev)
        {
            auto& L = SHARD_LOCAL_CHECKED();
            auto& R = L.world;
            auto& D = L.events;

            if (!ev.buf || !ev.buf->Buffer()) return;
            auto& tx = R.get<CompTransportTx>(ev.e);
            tx.queue.push_back(PendingTx{ ev.buf, ev.reason, ev.size });
            tx.bytesQueued += ev.size;

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
               D.enqueue<EvTxFlush>(EvTxFlush{ ev.e });
            }
        }

        void OnFlush(const EvTxFlush& ev)
        {
            auto& L = SHARD_LOCAL_CHECKED();
            auto& R = L.world;
            auto& D = L.events;

            if (!R.all_of<CompTransportTx, CompEndpoint>(ev.e)) return;
            auto& tx = R.get<CompTransportTx>(ev.e);
            if (tx.queue.empty()) return;

            // (1) ���� ACK Ÿ�Ӿƿ��̸� standalone ACK_ONLY ��Ŷ ���� (Piggyback �� ã�� ���)
            if (R.all_of<CompReliability>(ev.e)) 
            {
                auto& cr = R.get<CompReliability>(ev.e);
                if (cr.hasPendingAck) 
                {
                    uint64 now = utils::Clock::Instance().NowNs();
                    if ((now - cr.firstPendingAckTime_ns) >= MAX_DELAY_TICK_PIGGYBACK_ACK) 
                    {
                        auto ackBuf = PacketBuilder::CreateReliabilityAckPacket(cr.pendingAckSeq, cr.pendingAckBitfield);
                        tx.queue.push_back(PendingTx{ ackBuf, eTxReason::ACK_ONLY, ackBuf->WriteSize() });
                    }
                }
            }

            // (2) �켱���� ����
            ranges::stable_sort(tx.queue, [](const PendingTx& a, const PendingTx& b) { return Priority(a.reason) < Priority(b.reason); });

            // (3) Piggyback 1ȸ �õ� (�ڿ������� Reliable �ĺ�)
            bool piggyOK = false;
            if (R.all_of<CompReliability>(ev.e)) 
            {
                auto& cr = R.get<CompReliability>(ev.e);
                if (cr.hasPendingAck) 
                {
                    for (int i = static_cast<int32>(tx.queue.size()) - 1; i >= 0; --i) 
                    {
                        if (TryPiggyback(ev.e, tx.queue[i])) 
                        {
                            piggyOK = true;
                            break;
                        }
                    }
                }
            }

            // (4) �����ߴٸ� ACK_ONLY �׸� ����
            if (piggyOK) 
            {
                std::erase_if(tx.queue, [](const PendingTx& p) { return p.reason == eTxReason::ACK_ONLY; });
            }
            // (piggy ���� + timeout�̸� ACK_ONLY ���� �����Ƿ� �״�� ����)


            for (auto& p : tx.queue)
            {
                PacketAnalysis an = PacketBuilder::AnalyzePacket(p.buf->Buffer(), p.buf->WriteSize());
                if (an.IsReliable())
                {
                	D.enqueue<EvNsOnSendR>(EvNsOnSendR{ ev.e });
                }

                D.enqueue<EvNsOnSend>(EvNsOnSend{ ev.e });
            }


            auto& ep = R.get<CompEndpoint>(ev.e);

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
        auto& R = L.world;
    	auto& D = L.events;
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
        auto& D = L.events;
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
                D.enqueue<EvTxFlush>(EvTxFlush{ e });
            }
        }
    }

    
    // Helper

    inline void EnqueueSend(entt::entity e, const Sptr<SendBuffer>& buf, eTxReason reason)
    {
        auto& L = utils::exec::ShardTLS::GetCurrentChecked();
        auto& R = L.world;
        auto& D = L.events;

        if (!R.all_of<CompTransportTx>(e))
            R.emplace<CompTransportTx>(e);

        PacketAnalysis an = PacketBuilder::AnalyzePacket(buf->Buffer(), buf->WriteSize());

        if (an.IsNeedToFragmentation())
        {
            D.enqueue<EvFgFragmentize>(EvFgFragmentize{ e, buf, an });
            return;
        }

        // 2. �ŷڼ� �ʿ�
        if (an.IsReliable() && R.all_of<CompReliability>(e))
        {
            uint16 seq = AllocSeqRange(R, e, 1);
            auto* ph = reinterpret_cast<PacketHeader*>(buf->Buffer());
            ph->SetSequence(seq);

            uint64 now = utils::Clock::Instance().NowNs();
            uint32 size = buf->WriteSize();

            D.enqueue<EvReSendR>(EvReSendR{ e, buf, seq, size, now });
            return;
        }

        uint32 sz = (buf && buf->Buffer()) ? buf->WriteSize() : 0;
        D.enqueue<EvTxEnqueue>(EvTxEnqueue{ e, buf, reason, sz });
    }
}
