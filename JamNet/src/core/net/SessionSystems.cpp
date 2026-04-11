#include "pch.h"
#include "jamnet/core/net/SessionSystems.h"
#include "jamnet/core/executor/ShardTLS.h"
#include <limits>


namespace jam::net
{

    namespace
    {
        void EnqueueHandshakeReply(entt::registry& R, const entt::entity e, const eSystemPacketId id, const TransmissionWaitingQueue::Priority priority)
        {
            if (auto* tx = R.try_get<TransmissionWaitingQueue>(e))
                tx->Enqueue(PacketBuilder::CreateHandshakePacket(id), priority);
        }

        void NotifyEstablished(entt::registry& R, const entt::entity e, const uint64 now_ns)
        {
            if (!R.valid(e)) return;

            auto& info = R.get<SessionInfo>(e);
            info.state              = SessionInfo::CONNECTED;
            info.connectedTime_ns   = now_ns;
            info.lastRecvTime_ns    = now_ns;
            info.lastSendTime_ns    = now_ns;

            if (auto* ts = R.try_get<TimeSyncState>(e))
            {
                ts->lastPingSend_ns     = now_ns;
                ts->lastPongRecv_ns     = now_ns;
                ts->lastT2ServerRecv_ns = now_ns;
                ts->lastT4ClientRecv_ns = now_ns;
            }

            if (info.session)
                info.session->OnLinkEstablished();
        }

        void NotifyTerminated(entt::registry& R, const entt::entity e)
        {
            if (!R.valid(e)) return;

            auto& info = R.get<SessionInfo>(e);
            info.state = SessionInfo::DISCONNECTED;

            if (info.session)
                info.session->OnLinkTerminated();
        }

        uint32 ClampPendingReliableCount(const ReliabilityState& reliability)
        {
            return static_cast<uint32>(std::min<size_t>(reliability.reliablePendings.size(), std::numeric_limits<uint32>::max()));
        }

        void SyncPendingReliableMetrics(const ReliabilityState& reliability, profile::RudpMetrics* metrics)
        {
            if (!metrics)
                return;

            metrics->pendingReliableNow  = ClampPendingReliableCount(reliability);
            metrics->pendingReliablePeak = std::max(metrics->pendingReliablePeak, metrics->pendingReliableNow);
        }

        void MarkRetransmitScheduled(profile::RudpMetrics* metrics, ReliabilityState::PendingPacket& pkt, const uint64 now_ns, const bool timeoutTriggered)
        {
            pkt.lastRetransmitTime_ns = now_ns;
            pkt.retryCount++;

            if (!metrics)
                return;

            if (timeoutTriggered)
                metrics->rtxTimeoutPackets++;

            metrics->maxRtxPerPacket = std::max(metrics->maxRtxPerPacket, static_cast<uint32>(pkt.retryCount));
        }

        void MarkRetransmitGiveup(profile::RudpMetrics* metrics, ReliabilityState::PendingPacket& pkt)
        {
            if (pkt.countedGiveup)
                return;

            pkt.countedGiveup = true;

            if (metrics && (pkt.retryCount > 0 || pkt.hasRetransmitted))
                metrics->rtxGiveupPackets++;
        }

        void ProcessAck(entt::registry& R, const entt::entity e, const uint16 latestSeq, const uint32 wnd, const uint64 now_ns)
        {
            auto* reliability = R.try_get<ReliabilityState>(e);
            if (!reliability) return;

            struct AckedMeta
            {
                uint64 sendTime_ns = 0;
                uint64 lastRetransmitTime_ns = 0;
                bool   hasRetransmitted = false;
                uint32 size        = 0;
            };

            std::array<AckedMeta, ACK_WINDOW_SIZE + 1> acked{};
            uint32 ackedCount = 0;

            auto capture = [&](uint16 seq)
                {
                    const auto* pkt = reliability->TryGetPending(seq);
                    if (!pkt || ackedCount >= acked.size())
                        return;

                    acked[ackedCount++] = AckedMeta{
                        .sendTime_ns           = pkt->sendTime_ns,
                        .lastRetransmitTime_ns = pkt->lastRetransmitTime_ns,
                        .hasRetransmitted      = pkt->hasRetransmitted,
                        .size                  = pkt->buf ? pkt->buf->WriteSize() : 0
                    };
                };

            capture(latestSeq);
            for (uint16 i = 1; i <= ACK_WINDOW_SIZE; ++i)
            {
                if (wnd & (1u << (i - 1)))
                    capture(static_cast<uint16>(latestSeq - i));
            }

            reliability->ProcessAck(latestSeq, wnd);

            uint32 ackedBytes = 0;
            if (auto* metrics = R.try_get<profile::RudpMetrics>(e))
            {
	            for (uint32 i = 0; i < ackedCount; ++i)
	            {
                    if (acked[i].sendTime_ns != 0 && now_ns >= acked[i].sendTime_ns)
                        metrics->AddDeliveryLatency(now_ns - acked[i].sendTime_ns);

                    metrics->reliableAckedPackets++;
                    metrics->reliableAckedBytes += acked[i].size;

                    if (acked[i].hasRetransmitted)
                    {
                        metrics->rtxAckedPackets++;
                        if (acked[i].lastRetransmitTime_ns != 0 && now_ns >= acked[i].lastRetransmitTime_ns)
                            metrics->AddRecoveryLatency(now_ns - acked[i].lastRetransmitTime_ns);
                    }
                    else
                    {
                        metrics->firstSendAckedPackets++;
                    }

                    ackedBytes += acked[i].size;
	            }

                SyncPendingReliableMetrics(*reliability, metrics);
            }
            else
            {
                for (uint32 i = 0; i < ackedCount; ++i)
                    ackedBytes += acked[i].size;
            }

            if (ackedBytes > 0)
            {
                if (auto* congestion = R.try_get<CongestionState>(e))
                    congestion->OnAck(ackedBytes);
            }
        }

        void ProcessNackForChannel(entt::registry& R, const entt::entity e, const uint16 missingSeq, const uint32 wnd, const uint64 now_ns)
        {
            auto* reliability = R.try_get<ReliabilityState>(e);
            auto* txQueue     = R.try_get<TransmissionWaitingQueue>(e);
            if (!reliability || !txQueue)
                return;

            auto* metrics = R.try_get<profile::RudpMetrics>(e);

            auto trigger = [&](uint16 seq)
                {
                    auto* pkt = reliability->TryGetPending(seq);
                    if (!pkt || !pkt->buf)
                        return;

                    if (pkt->retryCount >= MAX_RETRY)
                    {
                        JAMNET_LOG_ERROR("[Retransmit] Packet seq={} exceeded max retry", seq);
                        MarkRetransmitGiveup(metrics, *pkt);
                        
                        // disconnect?
                     	
                     	return;
                    }

                    txQueue->Enqueue(pkt->buf, TxPriority::RETRANSMIT);
                    MarkRetransmitScheduled(metrics, *pkt, now_ns, false);

                    if (auto* congestion = R.try_get<CongestionState>(e))
                        congestion->OnLoss();
                };

            trigger(missingSeq);
            for (uint16 i = 1; i <= ACK_WINDOW_SIZE; ++i)
            {
                if (wnd & (1u << (i - 1)))
                    trigger(static_cast<uint16>(missingSeq + i));
            }
        }

        void DispatchApplicationPacket(RecvContext& ctx)
        {
            auto& R = ctx.L.registry;
            auto& sessInfo = R.get<SessionInfo>(ctx.e);

            switch (ctx.view.Type())
            {
            case ePacketType::RPC:
                if (auto* metrics = R.try_get<profile::RudpMetrics>(ctx.e))
                    metrics->appDeliveredPayloadBytes += ctx.view.PayloadSize();

                RPC::HandleIncomingPacket(R, ctx.e, ctx.view, ctx.buf);
                return;

            case ePacketType::CUSTOM:
                if (auto* metrics = R.try_get<profile::RudpMetrics>(ctx.e))
                    metrics->appDeliveredPayloadBytes += ctx.view.PayloadSize();

                if (sessInfo.session)
                    sessInfo.session->HandleCustomPacket(ctx.view);
                return;

            default:
                return;
            }
        }

        void DrainOrderedPackets(ShardLocal& L, const entt::entity e)
        {
            auto& R = L.registry;

            auto* seqState   = R.try_get<SequenceState>(e);
            auto* orderState = R.try_get<OrderState>(e);
            if (!seqState || !orderState)
                return;

            while (true)
            {
                auto buffered = orderState->PopOrderedPackets(seqState->expectedOrderedSeq);
                if (buffered.empty())
                    break;

                for (auto& pkt : buffered)
                {
                    if (!pkt.buf)
                        continue;

                    PacketView view = PacketView::Parse(pkt.buf->ReadPos(), pkt.buf->DataSize());
                    if (!view.IsValid())
                    {
                        JAMNET_LOG_WARN("[Ordering] Buffered packet became invalid");
                        continue;
                    }

                    RecvContext bufferedCtx{
                        .L              = L,
                        .e              = e,
                        .view           = view,
                        .buf            = pkt.buf,
                        .now_ns         = NOW_NS(),
                        .ingressTime_ns = pkt.recvTime_ns,
                        .orderedSpan    = pkt.span
                    };

                    DispatchApplicationPacket(bufferedCtx);
                }
            }
        }


        bool TryPiggybackAck(ShardLocal& L, const entt::entity e, TxPendingPacket& pkt)
        {
            auto& R = L.registry;
            auto* reliability = R.try_get<ReliabilityState>(e);
            if (!reliability || !reliability->ackDirty || pkt.priority != TxPriority::NORMAL) 
                return false;

            auto pktView = PacketView::Parse(pkt.buf->Buffer(), pkt.size);
            if (!pktView.IsValid())
                return false;

            const uint32 curTotalSize = pktView.TotalSize();
            const uint32 allocSize    = pkt.buf->AllocSize();

            if (allocSize < curTotalSize + sizeof(ACK_DATA))
                return false;

            auto* ack = reinterpret_cast<ACK_DATA*>(pkt.buf->Buffer() + curTotalSize);
            ack->latestSeq  = reliability->pendingAckSeq;
            ack->wnd        = reliability->pendingAckBitfield;

            const uint16 newTotalSize = static_cast<uint16>(curTotalSize + sizeof(ACK_DATA));
            pktView.Header()->SetSize(newTotalSize);
            pktView.Header()->SetFlags(pktView.Header()->GetFlags() | static_cast<uint8>(PacketFlags::PIGGYBACK_ACK));

            pkt.buf->SetWriteSize(newTotalSize);
            pkt.size = newTotalSize;

            reliability->ClearPendingAck();
            return true;
        }


        bool TryConsumeTailAck(RecvContext& ctx)
        {
            auto& R = ctx.L.registry;

            if (!HasFlag(ctx.view.Flags(), PacketFlags::PIGGYBACK_ACK))
                return false;

            const uint32 payloadSize = ctx.view.PayloadSize();
            if (payloadSize < sizeof(ACK_DATA))
                return false;

            const BYTE* tail = ctx.view.Payload() + (payloadSize - sizeof(ACK_DATA));
            const auto* ack  = reinterpret_cast<const ACK_DATA*>(tail);

            ProcessAck(R, ctx.e, ack->latestSeq, ack->wnd, ctx.now_ns);

            ctx.view.Header()->SetFlags(ClearFlag(ctx.view.Header()->GetFlags(), PacketFlags::PIGGYBACK_ACK));
            ctx.view.Header()->SetSize(static_cast<uint16>(ctx.view.TotalSize() - sizeof(ACK_DATA)));
            ctx.view = PacketView::Parse(ctx.buf->ReadPos(), ctx.buf->DataSize());

            return ctx.view.IsValid();
        }


        /// 우선순위 비교 함수
        int32 GetPriority(TransmissionWaitingQueue::Priority priority)
        {
            switch (priority)
            {
            case TransmissionWaitingQueue::CONTROL:     return 0;
            case TransmissionWaitingQueue::RETRANSMIT:  return 1;
            case TransmissionWaitingQueue::ACK_ONLY:    return 2;
            case TransmissionWaitingQueue::NORMAL:      return 3;
            default: return 3;
            }
        }
    }



    // ============================================================
    //  Bootstrap Systems
    // ============================================================

    void BootstrapNetworkSystems(ShardLocal& L)
    {
        auto& R = L.registry;
        if (R.ctx().contains<NetworkSystemBootstrappedTag>())
            return;

        SystemSessionTimeout(L, 0, 0);
        SystemSessionKeepalive(L, 0, 0);
        SystemRpcTimeout(L, 0, 0);
        SystemTransportFlush(L, 0, 0);
        SystemRetransmit(L, 0, 0);
        SystemFragmentCleanup(L, 0, 0);
        SystemHandshakeTimeout(L, 0, 0);
        SystemNetworkStats(L, 0, 0);

        R.ctx().emplace<NetworkSystemBootstrappedTag>();

        JAMNET_LOG_INFO("[SessionSystems] Network systems bootstrapped");
    }

    void BootstrapSessionEntity(ShardLocal& L, entt::entity e, Session* session)
    {
        auto& R = L.registry;
        uint64 now_ns = NOW_NS();

        // common component
        if (!R.all_of<SessionInfo>(e))                    R.emplace<SessionInfo>(e, SessionInfo::FromSession(session, now_ns));
        if (!R.all_of<profile::SessionTotalTraffic>(e))   R.emplace<profile::SessionTotalTraffic>(e);
        if (!R.all_of<TransmissionWaitingQueue>(e))       R.emplace<TransmissionWaitingQueue>(e);
        if (!R.all_of<RpcState>(e))                       R.emplace<RpcState>(e);


        // UDP-only component
        if (session->IsUdp())
        {
            if (!R.all_of<TimeSyncState>(e))
            {
                auto& ts = R.emplace<TimeSyncState>(e);
                ts.bIsServerSide        = session->IsServerSide();
                ts.lastPingSend_ns      = now_ns;
                ts.lastPongRecv_ns      = now_ns;
                ts.lastT2ServerRecv_ns  = now_ns;
                ts.lastT4ClientRecv_ns  = now_ns;
            }

            if (!R.all_of<HandshakeState>(e))   R.emplace<HandshakeState>(e);
            if (!R.all_of<SequenceState>(e))    R.emplace<SequenceState>(e);
            if (!R.all_of<OrderState>(e))       R.emplace<OrderState>(e);
            if (!R.all_of<ReliabilityState>(e)) R.emplace<ReliabilityState>(e);
            if (!R.all_of<CongestionState>(e))  R.emplace<CongestionState>(e);
            if (!R.all_of<FragmentState>(e))    R.emplace<FragmentState>(e);

            if (!R.all_of<profile::RudpMetrics>(e))      R.emplace<profile::RudpMetrics>(e);
        }

        if (!R.all_of<profile::LinkQualityState>(e))     R.emplace<profile::LinkQualityState>(e);
        if (!R.all_of<profile::TrafficSampleState>(e))   R.emplace<profile::TrafficSampleState>(e);

        JAMNET_LOG_DEBUG("[SessionSystems] [Thread #{}] Session entity {} initialized, protocol = {}", tl_ThreadId, static_cast<uint32>(e), session->IsUdp() ? "udp" : "tcp");
    }

    // ============================================================
    // Packet Processing - Incoming
    // ============================================================


    bool IncomingSequencingProcess(RecvContext& ctx)
    {
        auto& R        = ctx.L.registry;
        auto* sequence = R.try_get<SequenceState>(ctx.e);
        if (!sequence) return true;

        const eChannelType ch  = ctx.view.Channel();
        const uint16       seq = ctx.view.Sequence();

        if (ch == eChannelType::UNRELIABLE_SEQUENCED)
        {
            if (!sequence->IsNewerSequenced(seq))
            {
                ctx.shouldDrop = true;
                return false;
            }
            sequence->UpdateSequencedLatest(seq);
        }

        return true;
    }

    bool IncomingOrderingProcess(RecvContext& ctx)
    {
        auto& R = ctx.L.registry;
        const eChannelType ch = ctx.view.Channel();

        if (ch != eChannelType::RELIABLE_ORDERED)
            return true;

        auto* seqState   = R.try_get<SequenceState>(ctx.e);
        auto* orderState = R.try_get<OrderState>(ctx.e);
        if (!seqState || !orderState)
            return false;

        const uint16 orderedSeq  = ctx.view.OrderedSequence();
        const uint16 orderedSpan = std::max<uint16>(1, ctx.orderedSpan);
        const uint16 expectedSeq = seqState->expectedOrderedSeq;

        if (orderedSeq == expectedSeq)
        {
            seqState->expectedOrderedSeq = static_cast<uint16>(expectedSeq + orderedSpan);
            ctx.flushOrderedPending = true;
            return true;
        }

        if (SeqGreater(orderedSeq, expectedSeq))
        {
            if (auto* metrics = R.try_get<profile::RudpMetrics>(ctx.e))
                metrics->outOfOrderPackets++;

            if (orderState->pendings.size() >= OrderState::kMaxRecvBufferSize)
            {
                JAMNET_LOG_WARN("[Ordering] Recv buffer overflow");
                ctx.shouldDrop = true;
                return false;
            }

            if (!orderState->StoreRecvPacket(orderedSeq, orderedSpan, ctx.buf, ctx.now_ns))
            {
                ctx.shouldDrop = true;
                if (auto* metrics = R.try_get<profile::RudpMetrics>(ctx.e))
                    metrics->duplicatePackets++;
                return false;
            }
            ctx.needsReordering = true;

            // NACK 전송
            if (auto* reliability = R.try_get<ReliabilityState>(ctx.e))
            {
                const uint64 now = ctx.now_ns;

                if (now - reliability->lastNackTime_ns >= NACK_THROTTLE_INTERVAL_NS 
                    && !reliability->sentNackSeqs.contains(expectedSeq) 
                    && SeqGreater(orderedSeq, expectedSeq))
                {
                    const uint32 nackWnd = reliability->BuildNackWindow(expectedSeq);
                    const auto   nackBuf = PacketBuilder::CreateNackPacket(NACK_DATA(expectedSeq, nackWnd));

                    if (auto* txQueue = R.try_get<TransmissionWaitingQueue>(ctx.e))
                    {
                        txQueue->Enqueue(nackBuf, TxPriority::CONTROL);
                    }

                    reliability->lastNackTime_ns = now;
                    reliability->sentNackSeqs.insert(expectedSeq);
                }
            }

            return false;
        }

        ctx.shouldDrop = true;
        if (auto* metrics = R.try_get<profile::RudpMetrics>(ctx.e))
            metrics->duplicatePackets++;

        return false;
    }

    bool IncomingReliabilityProcess(RecvContext& ctx)
    {
        auto& R = ctx.L.registry;
        const eChannelType ch = ctx.view.Channel();

        if (!IsReliableChannel(ch))
            return true;

        auto* reliability = R.try_get<ReliabilityState>(ctx.e);
        if (!reliability)
            return false;

        const uint16 packetSeq = ctx.view.Sequence();

        if (!SeqGreater(packetSeq, reliability->latestRecvSeq - ACK_TRACK_SIZE))
            return false;

        if (!(reliability->ackTrack.none() && reliability->latestRecvSeq == 0))
        {
            if (!SeqGreater(packetSeq, reliability->latestRecvSeq))
            {
                const uint16 dist = SeqDistance(reliability->latestRecvSeq, packetSeq);
                if (dist >= ACK_TRACK_SIZE)
                {
                    ctx.shouldDrop = true;
                    return false;
                }

                if (reliability->ackTrack.test(dist))
                {
                    ctx.shouldDrop = true;
                    if (auto* metrics = R.try_get<profile::RudpMetrics>(ctx.e))
                        metrics->duplicatePackets++;
                    return false;
                }
            }
        }

        reliability->MarkReceived(packetSeq, ctx.now_ns);
        return true;
    }

    bool IncomingFragmentationProcess(RecvContext& ctx)
    {
        if (!ctx.view.IsFragmented())
            return true;

        auto& R = ctx.L.registry;
        auto* fragmentation = R.try_get<FragmentState>(ctx.e);
        if (!fragmentation) return false;

        const uint8  fragIdx    = ctx.view.FragmentIndex();
        const uint8  fragTotal  = ctx.view.TotalFragments();
        const uint16 fragmentId = ctx.view.Sequence() - fragIdx;

        if (fragTotal == 0 || fragIdx >= fragTotal)
            return false;

        const BYTE*  payload     = ctx.view.Payload();
        const uint32 payloadSize = ctx.view.PayloadSize();

        const uint64 now = ctx.now_ns;
        fragmentation->CleanupTimeouts(now);

        if (!fragmentation->AddFragment(fragmentId, fragTotal, fragIdx, payload, payloadSize, now))
            return false;

        auto reassembled = fragmentation->PopCompleted(fragmentId);
        if (!reassembled)
        {
            ctx.isReassembling = true;
            return false;
        }

        JAMNET_LOG_DEBUG("[Fragment] Reassembly complete: id={}, size={}", fragmentId, reassembled->size());

        if (auto* m = R.try_get<profile::RudpMetrics>(ctx.e))
            m->fragReassemblyCompleted++;

        if (ctx.view.Channel() == eChannelType::RELIABLE_ORDERED)
            ctx.orderedSpan = std::max<uint16>(1, fragTotal);

        const uint16 orderedBaseSeq = (ctx.view.Channel() == eChannelType::RELIABLE_ORDERED)
            ? static_cast<uint16>(ctx.view.OrderedSequence() - fragIdx)
            : 0;

        auto rebuilt = PacketBuilder::CreatePacket(
            ctx.view.Type(), ctx.view.Id(),
            ClearFlag(ctx.view.Flags(), PacketFlags::FRAGMENTED),
            ctx.view.Channel(),
            reassembled->data(), static_cast<uint32>(reassembled->size()),
            fragmentId,
            orderedBaseSeq);

        if (!rebuilt) return false;
        ctx.buf  = RecvBuffer::FromSpan(rebuilt->Buffer(), rebuilt->WriteSize());
        ctx.view = PacketView::Parse(ctx.buf->ReadPos(), ctx.buf->DataSize());

        return ctx.view.IsValid();
    }

    bool IncomingNetstatProcess(RecvContext& ctx)
    {
        return true;
    }

    void HandleSystemPacket(RecvContext& ctx)
    {
        auto& R         = ctx.L.registry;
        auto* handshake = R.try_get<HandshakeState>(ctx.e);
        auto* info      = R.try_get<SessionInfo>(ctx.e);
        auto* timesync  = R.try_get<TimeSyncState>(ctx.e);

        if (!handshake || !info || !timesync) return;

        const uint64 now_ns = ctx.now_ns;

        switch (U2E(eSystemPacketId, ctx.view.Id()))
        {
        case eSystemPacketId::CONNECT_SYN:
        {
            // 서버측: DISCONNECTED 상태에서 SYN 수신 -> SYNACK 응답
            if (handshake->state != HandshakeState::DISCONNECTED)
                return;

            handshake->state = HandshakeState::CONNECT_SYN_RECEIVED;
            EnqueueHandshakeReply(R, ctx.e, eSystemPacketId::CONNECT_SYNACK, TransmissionWaitingQueue::CONTROL);

            handshake->state                = HandshakeState::CONNECT_SYNACK_SENT;
            handshake->lastHandshakeTime_ns = now_ns;
            handshake->retryCount           = 0;

            info->state = SessionInfo::CONNECTING;
            break;
        }
        case eSystemPacketId::CONNECT_SYNACK:
        {
            // 클라이언트측: SYN_SENT 에서 SYNACK 수신 -> ACK 송신 + 연결 완료
            if (handshake->state != HandshakeState::CONNECT_SYN_SENT)
                return;

            handshake->state = HandshakeState::CONNECT_SYNACK_RECEIVED;
            EnqueueHandshakeReply(R, ctx.e, eSystemPacketId::CONNECT_ACK, TransmissionWaitingQueue::CONTROL);

            handshake->state                = HandshakeState::CONNECTED;
            handshake->lastHandshakeTime_ns = now_ns;

            ctx.L.defers.emplace_back([e = ctx.e, now_ns](entt::registry& rr) { NotifyEstablished(rr, e, now_ns); });
            break;
        }
        case eSystemPacketId::CONNECT_ACK:
        {
            // 서버측: SYNACK_SENT 에서 ACK 수신 -> 연결 완료
            if (handshake->state != HandshakeState::CONNECT_SYNACK_SENT)
                return;

            handshake->state                = HandshakeState::CONNECTED;
            handshake->lastHandshakeTime_ns = now_ns;

            ctx.L.defers.emplace_back([e = ctx.e, now_ns](entt::registry& rr) { NotifyEstablished(rr, e, now_ns); });
            break;
        }

        case eSystemPacketId::DISCONNECT_FIN:
        {
            // 상대가 종료 시작
            if (handshake->state == HandshakeState::CONNECTED)
            {
                handshake->state = HandshakeState::DISCONNECT_FIN_RECEIVED;
                EnqueueHandshakeReply(R, ctx.e, eSystemPacketId::DISCONNECT_FINACK, TransmissionWaitingQueue::CONTROL);

                handshake->state                = HandshakeState::DISCONNECT_FINACK_SENT;
                handshake->lastHandshakeTime_ns = now_ns;

                info->state = SessionInfo::DISCONNECTING;
            }
            else if (handshake->state == HandshakeState::DISCONNECT_FIN_SENT)
            {
                handshake->state = HandshakeState::CLOSING;
                EnqueueHandshakeReply(R, ctx.e, eSystemPacketId::DISCONNECT_FINACK, TransmissionWaitingQueue::CONTROL);

                handshake->lastHandshakeTime_ns = now_ns;
            }
            break;
        }
        case eSystemPacketId::DISCONNECT_FINACK:
        {
            if (handshake->state == HandshakeState::DISCONNECT_FIN_SENT || handshake->state == HandshakeState::CLOSING)
            {
                EnqueueHandshakeReply(R, ctx.e, eSystemPacketId::DISCONNECT_ACK, TransmissionWaitingQueue::CONTROL);

                handshake->state                = HandshakeState::TIME_WAIT;
                handshake->timeWaitStart_ns     = now_ns;
                handshake->lastHandshakeTime_ns = now_ns;

                info->state = SessionInfo::DISCONNECTING;
            }
            break;
        }
        case eSystemPacketId::DISCONNECT_ACK:
        {
            // 수동 종료측(상대 FIN에 FINACK 보낸 후) ACK 받으면 종료 완료
            if (handshake->state == HandshakeState::DISCONNECT_FINACK_SENT)
            {
                handshake->state = HandshakeState::DISCONNECTED;
                info->state      = SessionInfo::DISCONNECTED;

                ctx.L.defers.emplace_back([e = ctx.e](entt::registry& rr) { NotifyTerminated(rr, e); });
            }
            break;
        }

        case eSystemPacketId::PING:
	    {
		    if (ctx.view.PayloadSize() < sizeof(PING_DATA))
                return;

            const auto* ping = reinterpret_cast<const PING_DATA*>(ctx.view.Payload());

            timesync->lastT1ClientSend_ns = ping->t1App_ns;
            timesync->lastT2ServerRecv_ns = now_ns;
            timesync->lastSampleClient_ns = ping->t1App_ns;

            PONG_DATA pong{};

            pong.t1Wire_ns = ping->t1Wire_ns;
            pong.t2Wire_ns = ctx.ingressTime_ns; // wire ingress
            pong.t3Wire_ns = 0;                  // wire egress : fill in UdpRouter

            pong.t1App_ns = ping->t1App_ns;
            pong.t2App_ns = now_ns;
            pong.t3App_ns = NOW_NS();

            if (auto* txQueue = R.try_get<TransmissionWaitingQueue>(ctx.e))
            {
                txQueue->Enqueue(PacketBuilder::CreatePongPacket(pong), TxPriority::CONTROL);
            }
            break;
	    }

        case eSystemPacketId::PONG:
	    {
            if (ctx.view.PayloadSize() < sizeof(PONG_DATA))
                return;

            const auto*  pong = reinterpret_cast<const PONG_DATA*>(ctx.view.Payload());
            const uint64 t4_app  = now_ns;
            const uint64 t4_wire = ctx.ingressTime_ns;
	    
            timesync->lastPongRecv_ns     = t4_app;
            timesync->lastT2ServerRecv_ns = pong->t2App_ns;
            timesync->lastT3ServerSend_ns = pong->t3App_ns;
            timesync->lastT4ClientRecv_ns = t4_app;

            timesync->ProcessPingPong(
                pong->t1App_ns, 
                pong->t2App_ns, 
                pong->t3App_ns, 
                t4_app);

            if (auto* linkQuality = R.try_get<profile::LinkQualityState>(ctx.e))
            {
                // app RTT
                if (t4_app >= pong->t1App_ns && pong->t3App_ns >= pong->t2App_ns)
                {
                    const uint64 appPath = t4_app - pong->t1App_ns;
                    const uint64 appProc = pong->t3App_ns - pong->t2App_ns;
                    if (appPath >= appProc)
                        linkQuality->AddAppRttSample(static_cast<float>(appPath - appProc) / 1'000'000.0f);
                }

                // wire RTT
                if (pong->t1Wire_ns != 0 
                    && pong->t2Wire_ns != 0 
                    && pong->t3Wire_ns != 0
                    && t4_wire >= pong->t1Wire_ns 
                    && pong->t3Wire_ns >= pong->t2Wire_ns)
                {
                    const uint64 wirePath = t4_wire - pong->t1Wire_ns;
                    const uint64 wireProc = pong->t3Wire_ns - pong->t2Wire_ns;
                    if (wirePath >= wireProc)
                        linkQuality->AddWireRttSample(static_cast<float>(wirePath - wireProc) / 1'000'000.0f);
                }
            }

            break;
	    }

        default:
            break;
        }
    }

    void HandleAckPacket(RecvContext& ctx)
    {
        auto& R = ctx.L.registry;

        eChannelType ch = ctx.view.Channel();
        if (ch != eChannelType::UNRELIABLE_SEQUENCED)
            return;

        switch (U2E(eAckPacketId, ctx.view.Id()))
        {
        case eAckPacketId::ACK:
        {
            if (ctx.view.PayloadSize() < sizeof(ACK_DATA))
                return;

            const auto* ack = reinterpret_cast<const ACK_DATA*>(ctx.view.Payload());
            ProcessAck(R, ctx.e, ack->latestSeq, ack->wnd, ctx.now_ns);
            break;
        }
        case eAckPacketId::NACK:
        {
            if (ctx.view.PayloadSize() < sizeof(NACK_DATA))
                return;

            const auto* nack = reinterpret_cast<const NACK_DATA*>(ctx.view.Payload());
            ProcessNackForChannel(R, ctx.e, nack->missingSeq, nack->wnd, ctx.now_ns);
            break;
        }
        default:
            break;
        }
    }



    void PipelineIncomingPacket(RecvContext& ctx)
    {
        auto& R = ctx.L.registry;
        auto& sessInfo = R.get<SessionInfo>(ctx.e);
        sessInfo.lastRecvTime_ns = ctx.now_ns;

        if (auto* traffic = R.try_get<profile::SessionTotalTraffic>(ctx.e))
            traffic->OnRecv(ctx.view.Channel(), ctx.view.TotalSize());

        if (auto* m = R.try_get<profile::RudpMetrics>(ctx.e))
        {
            m->rxPackets++;
            m->rxBytes += ctx.view.TotalSize();
        }

        switch (ctx.view.Type())
        {
        case ePacketType::SYSTEM:
            HandleSystemPacket(ctx);
            return;

        case ePacketType::ACK:
            HandleAckPacket(ctx);
            return;

        default: break;
        }

        if (!IncomingSequencingProcess(ctx))    return;
        if (!IncomingReliabilityProcess(ctx))   return;
        if (!IncomingFragmentationProcess(ctx)) return;
        if (!IncomingNetstatProcess(ctx))       return;
        if (!TryConsumeTailAck(ctx) && HasFlag(ctx.view.Flags(), PacketFlags::PIGGYBACK_ACK)) return;
        if (!IncomingOrderingProcess(ctx))      return;

        DispatchApplicationPacket(ctx);

        if (ctx.flushOrderedPending)
            DrainOrderedPackets(ctx.L, ctx.e);
    }

    // ============================================================
    //  Packet Processing - Outgoing
    // ============================================================

    bool OutgoingFragmentationProcess(SendContext& ctx)
    {
        const auto ch = ctx.view.Channel();

        if (!ctx.view.IsNeedToFragmentation())
            return true;

        auto* seqState = ctx.L.registry.try_get<SequenceState>(ctx.e);
        if (!seqState) return false;

        const uint32 fullPayloadSize = ctx.view.PayloadSize();
        const uint16 fragTotal       = static_cast<uint16>((fullPayloadSize + FragmentState::kMaxFragmentPayloadSize - 1) / FragmentState::kMaxFragmentPayloadSize);
        if (fragTotal > FragmentState::kMaxFragments)
            return false;

        const BYTE*  basePayload    = ctx.view.Payload();
        const uint16 basePacketSeq  = seqState->AllocPacketSeq(fragTotal);

        bool isOrdered = IsOrderedChannel(ch);
        const uint16 baseOrderedSeq = isOrdered ? seqState->AllocOrderedSeq(fragTotal) : 0;

        auto& txQueue     = ctx.L.registry.get<TransmissionWaitingQueue>(ctx.e);
        auto* reliability = ctx.L.registry.try_get<ReliabilityState>(ctx.e);
        auto* metrics     = ctx.L.registry.try_get<profile::RudpMetrics>(ctx.e);

        if (IsReliableChannel(ch) && !reliability)
            return false;

        if (metrics)
            metrics->fragOriginalPayloadBytes += fullPayloadSize;
		
        for (auto i = 0; i < fragTotal; ++i)
        {
            const uint32 offset = i * FragmentState::kMaxFragmentPayloadSize;
            const uint32 chunk  = std::min<uint32>(FragmentState::kMaxFragmentPayloadSize, fullPayloadSize - offset);
            auto frag = PacketBuilder::CreatePacket(
                ctx.view.Type(),
                ctx.view.Id(),
                ctx.view.Flags() | PacketFlags::FRAGMENTED,
                ctx.view.Channel(),
                basePayload + offset,
                chunk,
                static_cast<uint16>(basePacketSeq + i),
                isOrdered ? static_cast<uint16>(baseOrderedSeq + i) : 0,
                static_cast<uint8>(i),
                static_cast<uint8>(fragTotal)
            );

            if (!frag) continue;

            if (IsReliableChannel(ch))
            {
                if (!reliability->StoreSendPacket(ch, frag, static_cast<uint16>(basePacketSeq + i), ctx.now_ns))
                    return false;

                SyncPendingReliableMetrics(*reliability, metrics);
            }

            if (metrics)
				metrics->fragWireBytes += frag->WriteSize();

            txQueue.Enqueue(frag, TxPriority::NORMAL);
        }

        ctx.bIsFragmentized = true;

        return true;
    }

    bool OutgoingSequencingProcess(SendContext& ctx)
    {
        auto* txQueue = ctx.L.registry.try_get<TransmissionWaitingQueue>(ctx.e);
        if (!txQueue) return false;
        auto* metrics = ctx.L.registry.try_get<profile::RudpMetrics>(ctx.e);

        const auto ch = ctx.view.Channel();
        if (ch == eChannelType::UNRELIABLE_UNORDERED || ch == eChannelType::TCP_DEFAULT)
        {
            txQueue->Enqueue(ctx.buf, ctx.priority);
            return true;
        }

        if (auto* seqState = ctx.L.registry.try_get<SequenceState>(ctx.e))
        {
	        const uint16 packetSeq = seqState->AllocPacketSeq(1);
            ctx.view.Header()->SetSequence(packetSeq);

            if (IsReliableChannel(ch))
            {
                auto* reliability = ctx.L.registry.try_get<ReliabilityState>(ctx.e);
                if (!reliability)
                    return false;

                if (IsOrderedChannel(ch))
                {
                    const uint16 orderdSeq = seqState->AllocOrderedSeq(1);
                    ctx.view.Header()->SetOrderedSequence(orderdSeq);
                }

                if (!reliability->StoreSendPacket(ch, ctx.buf, packetSeq, ctx.now_ns))
                    return false;

                SyncPendingReliableMetrics(*reliability, metrics);
            }
        }

        txQueue->Enqueue(ctx.buf, ctx.priority);
        return true;
    }


    void PipelineOutgoingPacket(SendContext& ctx)
    {
        auto& R = ctx.L.registry;
        if (!R.valid(ctx.e)) return;

        auto* txQueue = R.try_get<TransmissionWaitingQueue>(ctx.e);
        if (!txQueue) return;

        bool isUdp = R.get<SessionInfo>(ctx.e).session->IsUdp();

        if (!isUdp)
        {
			txQueue->Enqueue(ctx.buf, ctx.priority);
            txQueue->flushRequested = true;
            return;
        }

        if (isUdp && !OutgoingFragmentationProcess(ctx)) return;

        if (isUdp && !ctx.bIsFragmentized)
            if (!OutgoingSequencingProcess(ctx)) return;

        if (txQueue->ShouldFlush(ctx.now_ns))
        {
            txQueue->flushRequested = true;
        }
    }



    // ============================================================
    // Tick Systems
    // ============================================================

    void SystemSessionTimeout(ShardLocal& L, uint64 now_ns, uint64 dt_ns)
    {
        auto& R = L.registry;
        auto view = R.view<SessionInfo, TimeSyncState, HandshakeState>(); // UDP-only

        for (auto entity : view)
        {
            auto& info      = view.get<SessionInfo>(entity);
            auto& timesync  = view.get<TimeSyncState>(entity);
            auto& handshake = view.get<HandshakeState>(entity);

            if (info.state != SessionInfo::CONNECTED)           continue;
            if (handshake.state != HandshakeState::CONNECTED)   continue;
            if (!info.session || !info.session->IsReady())      continue;

            // 연결 직후 유예 (최소 2초 권장)
            if ((now_ns - info.connectedTime_ns) < 3_s)
                continue;

            const uint64 keepaliveAliveNs =
                timesync.bIsServerSide ? timesync.lastT2ServerRecv_ns : timesync.lastPongRecv_ns;

            // ping/pong 기준 + 실제 수신 기준 중 최신값 사용
            const uint64 lastAliveNs = std::max(keepaliveAliveNs, info.lastRecvTime_ns);

            if (now_ns <= lastAliveNs)
                continue;

            const uint64 delta = now_ns - lastAliveNs;
            if (delta <= SessionInfo::kTimeout_ns)
                continue;

            info.state = SessionInfo::DISCONNECTING;
            L.defers.emplace_back([entity](entt::registry& rr)
                {
                    if (!rr.valid(entity)) return;
                    if (auto* si = rr.try_get<SessionInfo>(entity); si && si->session)
                        si->session->Disconnect();
                });
        }
    }

    void SystemSessionKeepalive(ShardLocal& L, uint64 now_ns, uint64 dt_ns)
    {
        auto& R = L.registry;
        auto view = R.view<SessionInfo, TransmissionWaitingQueue, TimeSyncState, HandshakeState>();

        for (auto entity : view)
        {
            auto& info     = view.get<SessionInfo>(entity);
            auto& txQueue  = view.get<TransmissionWaitingQueue>(entity);
            auto& timeSync = view.get<TimeSyncState>(entity);

            if (info.state != SessionInfo::CONNECTED)
                continue;

            if (!info.session || !info.session->IsReady())
                continue;

            if (timeSync.bIsServerSide)
                continue;

            if (!timeSync.ShouldSendPing(now_ns))
                continue;

            PING_DATA ping{};
            ping.t1Wire_ns                = 0;          // wire send time : fill in UdpRouter
        	ping.t1App_ns                 = now_ns;

            ping.prev_t3_server_send_ns   = timeSync.lastT3ServerSend_ns;
            ping.prev_t4_client_recv_ns   = timeSync.lastT4ClientRecv_ns;

            txQueue.Enqueue(PacketBuilder::CreatePingPacket(ping), TransmissionWaitingQueue::CONTROL);

            timeSync.lastPingSend_ns      = now_ns;
            timeSync.lastT1ClientSend_ns  = now_ns;
            info.lastSendTime_ns          = now_ns;
        }
    }

    void SystemRpcTimeout(ShardLocal& L, uint64 now_ns, uint64 dt_ns)
    {
        auto& R = L.registry;
        auto view = R.view<RpcState>();

        for (auto entity : view)
        {
            auto& rpcState = view.get<RpcState>(entity);

            std::vector<uint32> timedOut = rpcState.GetTimedOutRequests(now_ns);

            for (uint32 requestId : timedOut)
            {
                auto awaitState = rpcState.PopRequest(requestId);
                if (awaitState && awaitState->onDone)
                {
                    awaitState->onDone(false);
                }

                JAMNET_LOG_WARN("[RPC] Request {} timed out", requestId);
            }
        }
    }

    void SystemTransportFlush(ShardLocal& L, uint64 now_ns, uint64 dt_ns)
    {
        auto& R = L.registry;
        auto view = R.view<TransmissionWaitingQueue, SessionInfo>();

        for (auto entity : view)
        {
            auto& txQueue = view.get<TransmissionWaitingQueue>(entity);
            auto& info = view.get<SessionInfo>(entity);

            if (!info.session)
                continue;
            if (txQueue.queue.empty())
                continue;
            if (!txQueue.ShouldFlush(now_ns))
                continue;

            const bool isUdp = info.session->IsUdp();
            auto* congestion = R.try_get<CongestionState>(entity);
            auto* reliabilityState = isUdp ? R.try_get<ReliabilityState>(entity) : nullptr;

            if (isUdp && R.all_of<ReliabilityState>(entity))
            {
                auto& reliability = R.get<ReliabilityState>(entity);

                bool piggybackOK = false;
                if (reliability.ackDirty)
                {
                    for (int32 i = static_cast<int32>(txQueue.queue.size()) - 1; i >= 0; --i)
                    {
                        if (TryPiggybackAck(L, entity, txQueue.queue[i]))
                        {
                            piggybackOK = true;
                            break;
                        }
                    }
                }

                if (!piggybackOK && reliability.ShouldSendAck(now_ns))
                {
                    auto ackBuf = PacketBuilder::CreateAckPacket(ACK_DATA(reliability.pendingAckSeq, reliability.pendingAckBitfield));
                    txQueue.Enqueue(ackBuf, TxPriority::ACK_ONLY);
                    reliability.ClearPendingAck();
                }
            }

            std::ranges::stable_sort(txQueue.queue, [](const TxPendingPacket& a, const TxPendingPacket& b) {
                    return GetPriority(a.priority) < GetPriority(b.priority);
                });

            std::vector<std::shared_ptr<SendBuffer>> batch;
            batch.reserve(txQueue.queue.size());

            std::vector<TxPendingPacket> remain;
            remain.reserve(txQueue.queue.size());

            auto* traffic = R.try_get<profile::SessionTotalTraffic>(entity);

            for (size_t i = 0; i < txQueue.queue.size(); ++i)
            {
                auto& pkt = txQueue.queue[i];

                PacketView v = PacketView::Parse(pkt.buf->Buffer(), pkt.size);
                if (!v.IsValid()) continue;

                ReliabilityState::PendingPacket* pending = nullptr;
                if (isUdp && reliabilityState && v.IsReliable())
                {
                    pending = reliabilityState->TryGetPending(v.Sequence());
                    if (pkt.priority == TxPriority::RETRANSMIT && !pending)
                        continue;
                }

                const bool bypassCongestion = (pkt.priority <= TxPriority::ACK_ONLY);

                if (!bypassCongestion && isUdp && congestion && v.IsReliable() && !congestion->CanSend(pkt.size))
                {
                    remain.insert(remain.end(), txQueue.queue.begin() + static_cast<std::ptrdiff_t>(i), txQueue.queue.end());
                    break;
                }

                batch.push_back(pkt.buf);

                if (traffic) traffic->OnSend(v.Channel(), pkt.size);

                const bool countedReliableOriginal = (pending != nullptr && pkt.priority != TxPriority::RETRANSMIT);

                if (auto* m = R.try_get<profile::RudpMetrics>(entity))
                {
                    m->txPackets++;
                    m->txBytes += pkt.size;

                    if (countedReliableOriginal)
                    {
                        m->reliableOriginalPackets++;
                        m->reliableOriginalBytes += pkt.size;
                    }

                    if (pkt.priority == TxPriority::RETRANSMIT)
                    {
                        m->rtxPackets++;
                        m->rtxBytes += pkt.size;

                        if (pending && !pending->hasRetransmitted)
                            m->rtxOriginalPackets++;
                    }

                    if (v.Type() == ePacketType::ACK && pkt.priority == TxPriority::ACK_ONLY)
                        m->ackStandalonePackets++;

                    if (HasFlag(v.Flags(), PacketFlags::PIGGYBACK_ACK))
                        m->ackPiggybackedPackets++;
                }

                if (pending)
                {
                    if (pkt.priority == TxPriority::RETRANSMIT)
                    {
                        pending->hasRetransmitted    = true;
                        pending->lastRetransmitTime_ns = now_ns;
                    }
                    else if (!pending->hasInitialSend)
                    {
                        pending->hasInitialSend      = true;
                        pending->sendTime_ns         = now_ns;
                        pending->lastRetransmitTime_ns = now_ns;
                    }
                }

                if (!bypassCongestion && isUdp && congestion && v.IsReliable())
                    congestion->OnSend(pkt.size);
            }

            txQueue.queue       = std::move(remain);
            txQueue.bytesQueued = 0;
            for (const auto& p : txQueue.queue) txQueue.bytesQueued += p.size;
            txQueue.flushRequested = !txQueue.queue.empty();

            if (batch.empty())
                continue;

            Session* session = info.session;
            GlobalExecutor::Instance().Submit(Job([session, batch = std::move(batch)]() mutable
                {
                    if (!session) return;

                    if (session->IsUdp())
                    {
                        auto* udp = static_cast<UdpSession*>(session);
                        udp->RegisterSend(batch);
                        return;
                    }

                    if (session->IsTcp())
                    {
                        auto* tcp = static_cast<TcpSession*>(session);
                        tcp->RegisterSend(batch);
                        return;
                    }
                }));

            txQueue.lastFlushTime_ns = now_ns;
            info.lastSendTime_ns     = now_ns;
        }
    }

    void SystemRetransmit(ShardLocal& L, uint64 now_ns, uint64 dt_ns)
    {
        auto& R = L.registry;
        auto view = R.view<ReliabilityState, TransmissionWaitingQueue, SessionInfo>();

        for (auto entity : view)
        {
            auto& reliability = view.get<ReliabilityState>(entity);
            auto& txQueue     = view.get<TransmissionWaitingQueue>(entity);
            auto& info        = view.get<SessionInfo>(entity);
            auto* metrics     = R.try_get<profile::RudpMetrics>(entity);

            if (info.state != SessionInfo::CONNECTED)
                continue;

            auto retransmitList = reliability.GetRetransmitNeeded(now_ns);
            for (uint16 seq : retransmitList)
            {
                auto* pkt = reliability.TryGetPending(seq);
                if (!pkt || !pkt->buf)
                    continue;

                if (pkt->retryCount >= MAX_RETRY)
                {
                    JAMNET_LOG_ERROR("[Retransmit] Packet seq={} exceeded max retry", seq);
                    MarkRetransmitGiveup(metrics, *pkt);
                    info.state = SessionInfo::DISCONNECTING;
                    break;
                }

                txQueue.Enqueue(pkt->buf, TransmissionWaitingQueue::RETRANSMIT);
                MarkRetransmitScheduled(metrics, *pkt, now_ns, true);

                JAMNET_LOG_DEBUG("[Retransmit] ch={} seq={}, retry={}", E2U(pkt->channel), seq, pkt->retryCount);
            }
        }
    }

    void SystemFragmentCleanup(ShardLocal& L, uint64 now_ns, uint64 dt_ns)
    {
        auto& R = L.registry;
        auto view = R.view<FragmentState>();

        for (auto entity : view)
        {
            auto& fragState = view.get<FragmentState>(entity);
            fragState.CleanupTimeouts(now_ns);

            if (auto* metrics = R.try_get<profile::RudpMetrics>(entity))
                metrics->fragReassemblyTimeoutDrops = fragState.timeoutDrops;
        }
    }

    void SystemHandshakeTimeout(ShardLocal& L, uint64 now_ns, uint64 dt_ns)
    {
        auto& R = L.registry;
        auto view = R.view<HandshakeState, SessionInfo, TransmissionWaitingQueue>();

        for (auto entity : view)
        {
            auto& handshake = view.get<HandshakeState>(entity);
            auto& info      = view.get<SessionInfo>(entity);
            auto& txQueue   = view.get<TransmissionWaitingQueue>(entity);


            if (handshake.state == HandshakeState::CONNECTED)
                continue;

            if (handshake.state == HandshakeState::TIME_WAIT)
            {
                if (now_ns - handshake.timeWaitStart_ns >= HandshakeState::kHandshakeMSL_ns * 2)
                {
                    handshake.state = HandshakeState::DISCONNECTED;
                    info.state      = SessionInfo::DISCONNECTED;
                    handshake.lastHandshakeTime_ns = 0;

                    L.defers.emplace_back([entity](entt::registry& rr)
                        {
                            NotifyTerminated(rr, entity);
                        });

                    JAMNET_LOG_INFO("[Handshake] TIME_WAIT finished, session {} closed", static_cast<uint32>(entity));
                }
                continue;
            }

            if (handshake.lastHandshakeTime_ns == 0)
                continue;

            if (now_ns - handshake.lastHandshakeTime_ns < HandshakeState::kHandshakeTimeout_ns)
                continue;

            handshake.retryCount++;

            if (handshake.retryCount >= HandshakeState::kMaxRetry)
            {
                handshake.state = HandshakeState::TIME_OUT;
                info.state      = SessionInfo::DISCONNECTED;
                handshake.lastHandshakeTime_ns = 0;

                L.defers.emplace_back([entity](entt::registry& rr)
                    {
                        NotifyTerminated(rr, entity);
                    });

                JAMNET_LOG_ERROR("[Handshake] Timeout, session entity= {} failed", static_cast<uint32>(entity));
                continue;
            }

            eSystemPacketId resendId{};
            bool shouldResend = true;

            switch (handshake.state)
            {
            case HandshakeState::CONNECT_SYN_SENT:
                resendId = eSystemPacketId::CONNECT_SYN;
                break;

            case HandshakeState::CONNECT_SYNACK_SENT:
                resendId = eSystemPacketId::CONNECT_SYNACK;
                break;

            case HandshakeState::DISCONNECT_FIN_SENT:
                resendId = eSystemPacketId::DISCONNECT_FIN;
                break;

            case HandshakeState::DISCONNECT_FINACK_SENT:
            case HandshakeState::CLOSING:
                resendId = eSystemPacketId::DISCONNECT_FINACK;
                break;

            default:
                shouldResend = false;
                break;
            }
			
            if (!shouldResend)
            {
                handshake.lastHandshakeTime_ns = now_ns;
                continue;
            }

            if (auto buf = PacketBuilder::CreateHandshakePacket(resendId))
            {
                txQueue.Enqueue(buf, TransmissionWaitingQueue::CONTROL);
                JAMNET_LOG_WARN("[Handshake] Retry {}/{} resend={}", handshake.retryCount, HandshakeState::kMaxRetry, E2U(resendId));
            }

            handshake.lastHandshakeTime_ns = now_ns;
        }
    }

    void SystemNetworkStats(ShardLocal& L, uint64 now_ns, uint64 dt_ns)
    {
        (void)now_ns;

        auto& R = L.registry;
        auto view = R.view<profile::SessionTotalTraffic, profile::TrafficSampleState>();

        const uint64 interval_ns = (dt_ns > 0) ? dt_ns : 1_s;

        for (auto entity : view)
        {
            auto& traffic = view.get<profile::SessionTotalTraffic>(entity);
            auto& trafficSample = view.get<profile::TrafficSampleState>(entity);
            auto* linkQuality = R.try_get<profile::LinkQualityState>(entity);
            auto* metrics = R.try_get<profile::RudpMetrics>(entity);

            profile::AccumulateSystemNetworkStats(trafficSample, traffic, linkQuality, metrics, interval_ns);
        }
    }



    // ============================================================
    // Helper Functions
    // ============================================================

    void ConnectHandshake(entt::entity e)
    {
        auto& L = SHARD_LOCAL_CHECKED();
        RegisterNetworkDomain(L);

        auto& R = L.registry;

        if (!R.valid(e))
        {
            JAMNET_LOG_WARN_LOC("Invalid entity");
            return;
        }

        auto* handshake = R.try_get<HandshakeState>(e);
        auto* info      = R.try_get<SessionInfo>(e);
        auto* txQueue   = R.try_get<TransmissionWaitingQueue>(e);
        if (!handshake || !info || !txQueue)
        {
            JAMNET_LOG_WARN_LOC("Missing components for hansahke");
            return;
        }

        if (handshake->state == HandshakeState::CONNECT_SYN_SENT)
        {
            JAMNET_LOG_TRACE("[Handshake] already sent CONNECT_SYN");
            return;
        }
        if (handshake->state != HandshakeState::DISCONNECTED)
        {
            JAMNET_LOG_TRACE("[Handshake] Connect ignored. state= {}", E2U(handshake->state));
            return;
        }

        auto buf = PacketBuilder::CreateHandshakePacket(eSystemPacketId::CONNECT_SYN);
        if (!buf) return;

        txQueue->Enqueue(buf, TxPriority::CONTROL);

        handshake->state                   = HandshakeState::CONNECT_SYN_SENT;
        handshake->lastHandshakeTime_ns    = NOW_NS();
        handshake->retryCount              = 0;

        info->state = SessionInfo::CONNECTING;

        JAMNET_LOG_DEBUG("[Hanshake] [Thread #{}] CONNECT_SYN sent. entity= {}", tl_ThreadId, static_cast<uint32>(e));
    }

    void DisconnectHandshake(entt::entity e)
    {
        auto& L = SHARD_LOCAL_CHECKED();
        auto& R = L.registry;

        if (!R.valid(e))
        {
            JAMNET_LOG_WARN_LOC("Invalid entity");
            return;
        }

        auto* handshake = R.try_get<HandshakeState>(e);
        auto* info      = R.try_get<SessionInfo>(e);
        auto* txQueue   = R.try_get<TransmissionWaitingQueue>(e);
        if (!handshake || !info || !txQueue)
        {
            JAMNET_LOG_WARN_LOC("Missing components for hansahke");
            return;
        }

        if (handshake->state != HandshakeState::CONNECTED && handshake->state != HandshakeState::DISCONNECT_FIN_SENT)
        {
            JAMNET_LOG_TRACE("[Handshake] Disconnect ignored. state= {}", E2U(handshake->state));
            return;
        }

        auto buf = PacketBuilder::CreateHandshakePacket(eSystemPacketId::DISCONNECT_FIN);
        if (!buf) return;

        txQueue->Enqueue(buf, TxPriority::CONTROL);

        handshake->state                = (handshake->state == HandshakeState::CONNECTED) ? HandshakeState::DISCONNECT_FIN_SENT : handshake->state;
        handshake->lastHandshakeTime_ns = NOW_NS();

        info->state = SessionInfo::DISCONNECTING;

        JAMNET_LOG_DEBUG("[Handshake] DISCONNECT_FIN sent. entity= {}", static_cast<uint32>(e));
    }

    void SendPacketToSession(entt::entity e, const std::shared_ptr<SendBuffer>& buf)
    {

    	auto& L = SHARD_LOCAL_CHECKED();
        auto& R = L.registry;
        if (!R.valid(e))
        {
            JAMNET_LOG_WARN_LOC("Invalid entity");
            return;
        }

        if (!R.all_of<SessionInfo>(e))
        {   
            JAMNET_LOG_WARN_LOC("Entity doesn't have SessionInfo");
            return;
        }

        PacketView view = PacketView::Parse(buf->Buffer(), buf->WriteSize());
        if (!view.IsValid())
        {
            JAMNET_LOG_WARN_LOC("Invalid Packet View");
            return;
        }

        SendContext ctx{
            .L          = L,
            .e          = e,
            .view       = view,
            .buf        = buf,
            .priority   = TransmissionWaitingQueue::NORMAL,
            .now_ns     = NOW_NS(),
        };

        PipelineOutgoingPacket(ctx);
    }

    void ProcessReceivedPacket(entt::entity e, const std::shared_ptr<RecvBuffer>& buf, uint64 ingressRecvTime_ns)
    {
        auto& L = SHARD_LOCAL_CHECKED();
        auto& R = L.registry;

        if (!R.valid(e))
        {
            JAMNET_LOG_WARN_LOC("Invalid Entity");
            return;
        }

        if (!R.all_of<SessionInfo>(e))
		{
            JAMNET_LOG_WARN_LOC("Entity doesn't have SessionInfo");
            return;
        }

        PacketView view = PacketView::Parse(buf->ReadPos(), buf->DataSize());
        if (!view.IsValid())
        {
            JAMNET_LOG_WARN("[Session] Invalid packet received");
            return;
        }

        const uint64 now_ns = NOW_NS();
        if (ingressRecvTime_ns == 0) ingressRecvTime_ns = now_ns;

        RecvContext ctx{
            .L              = L,
            .e              = e,
            .view           = view,
            .buf            = buf,
            .now_ns         = now_ns,
            .ingressTime_ns = ingressRecvTime_ns
        };

        PipelineIncomingPacket(ctx);
    }

    struct NetworkDomainRegisteredTag {};

    void RegisterNetworkDomain(ShardLocal& L, uint64 tickPeriod_ns)
    {
        auto& R = L.registry;
        if (R.ctx().contains<NetworkDomainRegisteredTag>())
            return;

        auto& group = L.domainGroups[{DOMAIN_NETWORK, 0}];
        group.tag           = { .domain = DOMAIN_NETWORK, .subType = 0 };
        group.tickPeriod_ns = tickPeriod_ns;

        // Systems
        group.systems.emplace_back(SystemSessionTimeout);
        group.systems.emplace_back(SystemSessionKeepalive);
        group.systems.emplace_back(SystemRpcTimeout);
        group.systems.emplace_back(SystemTransportFlush);
        group.systems.emplace_back(SystemRetransmit);
        group.systems.emplace_back(SystemFragmentCleanup);
        group.systems.emplace_back(SystemHandshakeTimeout);
        group.systems.emplace_back(SystemNetworkStats);

        const uint64 now_ns = NOW_NS();

        SystemSessionTimeout(L, now_ns, 0);
        SystemSessionKeepalive(L, now_ns, 0);
        SystemRpcTimeout(L, now_ns, 0);
        SystemTransportFlush(L, now_ns, 0);
        SystemRetransmit(L, now_ns, 0);
        SystemFragmentCleanup(L, now_ns, 0);
        SystemHandshakeTimeout(L, now_ns, 0);
        SystemNetworkStats(L, now_ns, 0);

        group.entityFilter = [](const entt::registry& R, entt::entity e) {
				return R.all_of<SessionInfo>(e);
            };

        JAMNET_LOG_DEBUG("[RegisterNetworkDomain] [ThreadId #{}] register network domain system", tl_ThreadId);

        R.ctx().emplace<NetworkDomainRegisteredTag>();
    }

} // namespace jam::net
