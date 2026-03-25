#include "pch.h"
#include "jamnet/core/net/SessionSystems.h"
#include "jamnet/core/executor/ShardTLS.h"


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
            {
                auto* udp = static_cast<UdpSession*>(info.session);
                udp->OnLinkEstablished();
            }
        }

        void NotifyTerminated(entt::registry& R, const entt::entity e)
        {
            if (!R.valid(e)) return;

            auto& info = R.get<SessionInfo>(e);
            info.state = SessionInfo::DISCONNECTED;

            if (info.session)
            {
                auto* udp = static_cast<UdpSession*>(info.session);
                udp->OnLinkTerminated();
            }
        }

        void ProcessAckForChannel(entt::registry& R, const entt::entity e, const eChannelType ch, const uint16 latestSeq, const uint32 wnd)
        {
            auto* reliability = R.try_get<ReliabilityState>(e);
            if (!reliability)
                return;

            reliability->ProcessAck(ch, latestSeq, wnd);

            if (auto* congestion = R.try_get<CongestionState>(e))
                congestion->OnAck(JAMNET_MTU);
        }

        void ProcessNackForChannel(entt::registry& R, const entt::entity e, const eChannelType ch, const uint16 missingSeq, const uint32 wnd)
        {
            auto* reliability = R.try_get<ReliabilityState>(e);
            auto* txQueue     = R.try_get<TransmissionWaitingQueue>(e);
            if (!reliability || !txQueue)
                return;

            auto& chData = reliability->GetChannelData(ch);

            auto triggerRTX = [&](uint16 seq)
                {
                    auto it = chData.pendings.find(seq);
                    if (it == chData.pendings.end())
                        return;

                    txQueue->Enqueue(it->second.buf, TransmissionWaitingQueue::RETRANSMIT);
                    it->second.lastRetransmitTime_ns = NOW_NS();
                    it->second.retryCount++;

                    if (auto* congestion = R.try_get<CongestionState>(e))
                        congestion->OnLoss();
                };

            triggerRTX(missingSeq);
            for (uint16 i = 1; i <= ACK_WINDOW_SIZE; ++i)
            {
                if (wnd & (1u << (i - 1)))
                    triggerRTX(static_cast<uint16>(missingSeq + i));
            }
        }



        /// Piggyback ACK 시도 (성공 시 true)
        bool TryPiggybackAck(ShardLocal& L, const entt::entity e, TransmissionWaitingQueue::PendingPacket& pkt)
        {
            auto& R = L.registry;

            // RELIABLE_* 채널만 ACK 가능
            auto* reliability = R.try_get<ReliabilityState>(e);
            if (!reliability) return false;

            // RELIABLE_ORDERED 채널의 pending ACK 확인
            auto& roData = reliability->GetChannelData(eChannelType::RELIABLE_ORDERED);
            if (!roData.hasPendingAck) return false;

            // NORMAL 패킷만 piggyback 대상
            if (pkt.priority != TransmissionWaitingQueue::NORMAL) return false;

            auto pktView = PacketView::Parse(pkt.buf->Buffer(), pkt.size);
            if (!pktView.IsValid()) return false;

            const uint32 curTotalSize = pktView.TotalSize();
            const uint32 allocSize    = pkt.buf->AllocSize();

            // 남은 공간 확인: 현재 크기 + ACK_DATA
            if (allocSize < curTotalSize + sizeof(ACK_DATA))
                return false;

            // ACK_DATA를 페이로드 뒤에 추가
            auto* ack = reinterpret_cast<ACK_DATA*>(pkt.buf->Buffer() + curTotalSize);
            ack->latestSeq  = roData.pendingAckSeq;
            ack->wnd        = roData.pendingAckBitfield;

            // 총 길이 갱신
            const uint16 newTotalSize = static_cast<uint16>(curTotalSize + sizeof(ACK_DATA));
            pktView.Header()->SetSize(newTotalSize);
            pktView.Header()->SetFlags(pktView.Header()->GetFlags() | static_cast<uint8>(PacketFlags::PIGGYBACK_ACK));

            // 버퍼 총 WriteSize 갱신
            pkt.buf->SetWriteSize(newTotalSize);
            pkt.size = newTotalSize;

            // pending ACK 리셋
            roData.hasPendingAck          = false;
            roData.pendingAckSeq          = 0;
            roData.pendingAckBitfield     = 0;
            roData.firstPendingAckTime_ns = 0;

            //JAMNET_LOG_TRACE("[Piggyback] ACK attached to packet: seq={}", ack->latestSeq);
            return true;
        }


        bool TryConsumeTailAck(RecvContext& ctx)
        {
            auto& R = ctx.L.registry;

            if (!ctx.view.IsReliable()) return false;
            if (!HasFlag(ctx.view.Flags(), PacketFlags::PIGGYBACK_ACK)) return false;

            const auto ch = ctx.view.Channel();
            if (ch != eChannelType::RELIABLE_ORDERED && ch != eChannelType::RELIABLE_UNORDERED)
                return false;

            const uint32 payloadSize = ctx.view.PayloadSize();
            if (payloadSize < sizeof(ACK_DATA))
                return false;

            const BYTE* tail = ctx.view.Payload() + (payloadSize - sizeof(ACK_DATA));
            const auto* ack  = reinterpret_cast<const ACK_DATA*>(tail);

            ProcessAckForChannel(R, ctx.e, ch, ack->latestSeq, ack->wnd);
            return true;
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
        if (!R.all_of<SessionInfo>(e))              R.emplace<SessionInfo>(e, SessionInfo::FromSession(session, now_ns));
        if (!R.all_of<NetworkCounter>(e))           R.emplace<NetworkCounter>(e);
        if (!R.all_of<TransmissionWaitingQueue>(e)) R.emplace<TransmissionWaitingQueue>(e);
        if (!R.all_of<RpcState>(e))                 R.emplace<RpcState>(e);


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
        }

#ifdef _DEBUG
        if (!R.all_of<CompNetworkStats>(e))     R.emplace<CompNetworkStats>(e);
#endif

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
            if (!sequence->IsNewer(ch, seq))
            {
                ctx.bShouldDrop = true;
                return false;
            }
            sequence->UpdateLatest(ch, seq);
        }

        return true;
    }

    bool IncomingOrderingProcess(RecvContext& ctx)
    {
        auto& R = ctx.L.registry;
        const eChannelType ch = ctx.view.Channel();

        if (ch != eChannelType::RELIABLE_ORDERED)
            return true;

        auto* sequence = R.try_get<SequenceState>(ctx.e);
        auto* order    = R.try_get<OrderState>(ctx.e);
        if (!sequence || !order)
            return false;

        const uint16 seq         = ctx.view.Sequence();
        const uint16 expectedSeq = sequence->expectedSeq[E2U(ch)];

        if (seq == expectedSeq)
        {
            sequence->UpdateExpected(ch, expectedSeq + 1);

            uint16 scanExpected = sequence->expectedSeq[E2U(ch)];
            auto buffered = order->PopOrderedPackets(scanExpected);
            
        	if (!buffered.empty())
            {
                JAMNET_LOG_TRACE("[Ordering] Delivered {} buffered packets", buffered.size());

                for (auto& p : buffered)
                {
                    if (p.buf)
                        ProcessReceivedPacket(ctx.e, p.buf);
                }
            }

            return true;                                                                                                
        }

        if (SeqGreater(seq, expectedSeq))
        {
            if (order->pendings.size() >= OrderState::kMaxRecvBufferSize)
            {
                JAMNET_LOG_WARN("[Ordering] Recv buffer overflow");
                ctx.bShouldDrop = true;
                return false;
            }

            order->StoreRecvPacket(seq, ctx.buf, ctx.now_ns);
            ctx.bNeedsReordering = true;

            // NACK 전송
            if (auto* reliability = R.try_get<ReliabilityState>(ctx.e))
            {
                auto& chData = reliability->GetChannelData(ch);
                const uint64 now = ctx.now_ns;

                if (now - chData.lastNackTime_ns >= NACK_THROTTLE_INTERVAL_NS 
                    && !chData.sentNackSeqs.contains(expectedSeq) 
                    && SeqGreater(seq, static_cast<uint16>(expectedSeq + 1)))
                {
                    const uint32 nackWnd = reliability->BuildNackWindow(ch, expectedSeq);
                    const auto   nackBuf = PacketBuilder::CreateNackPacket(NACK_DATA{
                    	.missingSeq = expectedSeq,
	                    .wnd        = nackWnd       });

                    if (auto* txQueue = R.try_get<TransmissionWaitingQueue>(ctx.e))
                    {
                        txQueue->Enqueue(nackBuf, TransmissionWaitingQueue::CONTROL);
                    }

                    chData.lastNackTime_ns = now;
                    chData.sentNackSeqs.insert(expectedSeq);
                }
            }

            return false;
        }

        ctx.bShouldDrop = true;
        return false;
    }

    bool IncomingReliabilityProcess(RecvContext& ctx)
    {
        auto& R = ctx.L.registry;
        const eChannelType ch = ctx.view.Channel();

        if (ch != eChannelType::RELIABLE_ORDERED && ch != eChannelType::RELIABLE_UNORDERED)
            return true;

        auto* sequence    = R.try_get<SequenceState>(ctx.e);
        auto* reliability = R.try_get<ReliabilityState>(ctx.e);
        if (!sequence || !reliability)
            return false;

        const uint16 seq = ctx.view.Sequence();
        auto& chData = reliability->GetChannelData(ch);

        if (!SeqGreater(seq, chData.latestAckSeq - ACK_TRACK_SIZE))
            return false;

        if (chData.ackTrack.test(seq % ACK_TRACK_SIZE))
        {
            ctx.bShouldDrop = true;
            return false;
        }

        chData.ackTrack.set(seq % ACK_TRACK_SIZE);
        if (SeqGreater(seq, chData.latestAckSeq))
            chData.latestAckSeq = seq;

        const uint16 expectedSeq = sequence->expectedSeq[E2U(ch)];
        if (seq == expectedSeq)
        {
            sequence->UpdateExpected(ch, expectedSeq + 1);

            uint16 nextExpected = sequence->expectedSeq[E2U(ch)];
            while (chData.ackTrack.test(nextExpected % ACK_TRACK_SIZE))
            {
                sequence->UpdateExpected(ch, ++nextExpected);
            }
        }

        const uint64 now = ctx.now_ns;
        if (!chData.hasPendingAck)
        {
            chData.hasPendingAck          = true;
            chData.pendingAckSeq          = seq;
            chData.firstPendingAckTime_ns = now;
        }
        else if (SeqGreater(seq, chData.pendingAckSeq))
        {
            chData.pendingAckSeq = seq;
        }

        chData.pendingAckBitfield = reliability->BuildAckWindow(ch);

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
            ctx.bIsReassembling = true;
            return false;
        }

        JAMNET_LOG_DEBUG("[Fragment] Reassembly complete: id={}, size={}", fragmentId, reassembled->size());

        ctx.buf  = RecvBuffer::FromSpan(reassembled->data(), static_cast<uint32>(reassembled->size()));
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

            timesync->lastT1ClientSend_ns = ping->t1_client_send_ns;
            timesync->lastT2ServerRecv_ns = now_ns;
            timesync->lastSampleClient_ns = ping->t1_client_send_ns;

            PONG_DATA pong{};
            pong.t1_client_send_ns = ping->t1_client_send_ns;
            pong.t2_server_recv_ns = now_ns;
            pong.t3_server_send_ns = NOW_NS();

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
            const uint64 t4   = now_ns;
	    
            timesync->lastPongRecv_ns     = t4;
            timesync->lastT2ServerRecv_ns = pong->t2_server_recv_ns;
            timesync->lastT3ServerSend_ns = pong->t3_server_send_ns;
            timesync->lastT4ClientRecv_ns = t4;

            timesync->ProcessPingPong(
                pong->t1_client_send_ns, 
                pong->t2_server_recv_ns, 
                pong->t3_server_send_ns, 
                t4);

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
        if (ch != eChannelType::RELIABLE_ORDERED && ch != eChannelType::RELIABLE_UNORDERED)
            return;

        switch (U2E(eAckPacketId, ctx.view.Id()))
        {
        case eAckPacketId::ACK:
        {
            if (ctx.view.PayloadSize() < sizeof(ACK_DATA))
                return;

            const auto* ack = reinterpret_cast<const ACK_DATA*>(ctx.view.Payload());
            ProcessAckForChannel(R, ctx.e, ch, ack->latestSeq, ack->wnd);
            break;
        }
        case eAckPacketId::NACK:
        {
            if (ctx.view.PayloadSize() < sizeof(NACK_DATA))
                return;

            const auto* nack = reinterpret_cast<const NACK_DATA*>(ctx.view.Payload());
            ProcessNackForChannel(R, ctx.e, ch, nack->missingSeq, nack->wnd);
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
        if (!IncomingOrderingProcess(ctx))      return;
        if (!IncomingReliabilityProcess(ctx))   return;
        if (!IncomingFragmentationProcess(ctx)) return;
        if (!IncomingNetstatProcess(ctx))       return;

        TryConsumeTailAck(ctx);

        switch (ctx.view.Type())
        {
        case ePacketType::RPC:
            RPC::HandleIncomingPacket(R, ctx.e, ctx.view, ctx.buf);
            return;
        case ePacketType::CUSTOM:
            sessInfo.session->HandleCustomPacket(ctx.view);
            return;

        default: break;
        }
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
        const uint16 fragTotal       = static_cast<uint8>((fullPayloadSize + FragmentState::kMaxFragmentPayloadSize - 1) / FragmentState::kMaxFragmentPayloadSize);
        if (fragTotal > FragmentState::kMaxFragments)
            return false;

        const BYTE*  basePayload = ctx.view.Payload();
        const uint16 baseSeq     = seqState->AllocSequence(ch, fragTotal);

        auto& txQueue = ctx.L.registry.get<TransmissionWaitingQueue>(ctx.e);
		
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
                static_cast<uint16>(baseSeq + i),
                static_cast<uint8>(i),
                static_cast<uint8>(fragTotal)
            );

            txQueue.Enqueue(frag, TxPriority::NORMAL);
        }

        ctx.bIsFragmentized = true;

        return true;
    }

    bool OutgoingSequencingProcess(SendContext& ctx)
    {
        auto* txQueue = ctx.L.registry.try_get<TransmissionWaitingQueue>(ctx.e);
        if (!txQueue) return false;

        const auto ch = ctx.view.Channel();
        if (ch == eChannelType::UNRELIABLE_UNORDERED || ch == eChannelType::TCP_DEFAULT)
        {
            txQueue->Enqueue(ctx.buf, ctx.priority);
            return true;
        }

        if (auto* seqState = ctx.L.registry.try_get<SequenceState>(ctx.e))
        {
	        uint16 seq = seqState->AllocSequence(ch, 1);

            auto* pktHeader = ctx.view.Header();
            pktHeader->SetSequence(seq);
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
            ping.t1_client_send_ns        = now_ns;
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

            vector<uint32> timedOut = rpcState.GetTimedOutRequests(now_ns);

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

            if (/*info.state == SessionInfo::DISCONNECTED ||*/ !info.session)
                continue;
            if (txQueue.queue.empty())
                continue;
            if (!txQueue.ShouldFlush(now_ns))
                continue;

            const bool isUdp = info.session->IsUdp();
            CongestionState* congestion = isUdp ? R.try_get<CongestionState>(entity) : nullptr;

            // ===== Piggyback ACK 로직 =====

            // 지연 ACK 타임아웃 확인 - standalone ACK_ONLY 패킷 삽입
            if (info.session->IsUdp() && R.all_of<ReliabilityState>(entity))
            {
                auto& reliability = R.get<ReliabilityState>(entity);
                auto& roData = reliability.GetChannelData(eChannelType::RELIABLE_ORDERED);

                if (roData.hasPendingAck)
                {
                    if ((now_ns - roData.firstPendingAckTime_ns) >= DELAY_PIGGYBACK_ACK_TIMEOUT_NS)
                    {
                        auto ackBuf = PacketBuilder::CreateAckPacket(ACK_DATA{
                        	.latestSeq  = roData.pendingAckSeq,
	                        .wnd        = roData.pendingAckBitfield });

                        txQueue.Enqueue(ackBuf, TxPriority::ACK_ONLY);
                    }
                }
            }

            // 우선순위 정렬
            std::ranges::stable_sort(txQueue.queue,[](const TxPendingPacket& a, const TxPendingPacket& b) {
                    return GetPriority(a.priority) < GetPriority(b.priority);
                });

            // Piggyback 1회 시도 (뒤에서부터 검색)
            bool piggybackOK = false;
            if (info.session->IsUdp() && R.all_of<ReliabilityState>(entity))
            {
                auto& reliability = R.get<ReliabilityState>(entity);
                auto& roData = reliability.GetChannelData(eChannelType::RELIABLE_ORDERED);

                if (roData.hasPendingAck)
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
            }

            // Piggyback 성공 시 ACK_ONLY 패킷 제거
            if (piggybackOK)
            {
                std::erase_if(txQueue.queue, [](const TxPendingPacket& p) { return p.priority == TransmissionWaitingQueue::ACK_ONLY; });
            }

            // ===== 전송 배치 생성 =====

            vector<shared_ptr<SendBuffer>> batch;
            batch.reserve(txQueue.queue.size());

            NetworkCounter*   counter = R.try_get<NetworkCounter>(entity);
            CompNetworkStats* stats   = R.try_get<CompNetworkStats>(entity);

            for (auto& pkt : txQueue.queue)
            {
                PacketView v = PacketView::Parse(pkt.buf->Buffer(), pkt.size);
                if (!v.IsValid())
                {
                    JAMNET_LOG_WARN_LOC("Invalid packet skipped during flush");
                    continue;
                }

                const bool bypassCongestion = (pkt.priority <= TransmissionWaitingQueue::ACK_ONLY);

                if (!bypassCongestion && isUdp && congestion && v.IsReliable())
                {
                    if (!congestion->CanSend(pkt.size))
                        break;
                }

                batch.push_back(pkt.buf);

                if (counter) counter->OnSend(pkt.size);
                if (stats)   stats->OnChannelSend(v.Channel(), pkt.size);
                if (!bypassCongestion && isUdp && congestion && v.IsReliable())
                    congestion->OnSend(pkt.size);
            }
            
            if (batch.empty()) continue;

        	Session* session = info.session;
            GlobalExecutor::Instance().Submit(Job([session, batch = std::move(batch)]() mutable
                {
                    if (!session)
                        return;

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


            txQueue.Clear();
            txQueue.lastFlushTime_ns = now_ns;

            info.lastSendTime_ns = now_ns;
        }
    }

    void SystemRetransmit(ShardLocal& L, uint64 now_ns, uint64 dt_ns)
    {
        auto& R = L.registry;
        auto view = R.view<ReliabilityState, TransmissionWaitingQueue, SessionInfo>();

        for (auto entity : view)
        {
            auto& reliability = view.get<ReliabilityState>(entity);
            auto& txQueue = view.get<TransmissionWaitingQueue>(entity);
            auto& info = view.get<SessionInfo>(entity);

            if (info.state != SessionInfo::CONNECTED)
                continue;

            // RELIABLE_ORDERED 재전송
            {
                auto retransmitList = reliability.GetRetransmitNeeded(eChannelType::RELIABLE_ORDERED, now_ns);

                for (uint16 seq : retransmitList)
                {
                    auto& chData = reliability.GetChannelData(eChannelType::RELIABLE_ORDERED);
                    auto it = chData.pendings.find(seq);
                    if (it == chData.pendings.end())
                        continue;

                    auto& pkt = it->second;

                    if (pkt.retryCount >= MAX_RETRY)
                    {
                        JAMNET_LOG_ERROR("[Retransmit] Packet seq={} exceeded max retry", seq);
                        info.state = SessionInfo::DISCONNECTING;
                        break;
                    }

                    txQueue.Enqueue(pkt.buf, TransmissionWaitingQueue::RETRANSMIT);
                    pkt.lastRetransmitTime_ns = now_ns;
                    pkt.retryCount++;

                    JAMNET_LOG_DEBUG("[Retransmit] RELIABLE_ORDERED seq={}, retry={}", seq, pkt.retryCount);
                }
            }

            // RELIABLE_UNORDERED 재전송
            {
                auto retransmitList = reliability.GetRetransmitNeeded(eChannelType::RELIABLE_UNORDERED, now_ns);

                for (uint16 seq : retransmitList)
                {
                    auto& chData = reliability.GetChannelData(eChannelType::RELIABLE_UNORDERED);
                    auto it = chData.pendings.find(seq);
                    if (it == chData.pendings.end())
                        continue;

                    auto& pkt = it->second;

                    if (pkt.retryCount >= MAX_RETRY)
                    {
                        JAMNET_LOG_ERROR_LOC("[Retransmit] Packet seq={} exceeded max retry", seq);
                        info.state = SessionInfo::DISCONNECTING;
                        break;
                    }

                    txQueue.Enqueue(pkt.buf, TransmissionWaitingQueue::RETRANSMIT);
                    pkt.lastRetransmitTime_ns = now_ns;
                    pkt.retryCount++;

                    JAMNET_LOG_DEBUG("[Retransmit] RELIABLE_UNORDERED seq={}, retry={}", seq, pkt.retryCount);
                }
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
                info.state      = SessionInfo::DISCONNECTING;

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
        auto& R = L.registry;
        auto view = R.view<NetworkCounter, CompNetworkStats>();

        for (auto entity : view)
        {
            auto& counter = view.get<NetworkCounter>(entity);
            auto& stats   = view.get<CompNetworkStats>(entity);

            static constexpr uint64 UPDATE_INTERVAL_NS = 1_s;
            stats.UpdateBandwidth(counter.totalSendBytes + counter.totalRecvBytes, UPDATE_INTERVAL_NS);
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

    void SendPacketToSession(entt::entity e, const shared_ptr<SendBuffer>& buf)
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

    void ProcessReceivedPacket(entt::entity e, const shared_ptr<RecvBuffer>& buf)
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

        RecvContext ctx{
            .L      = L,
            .e      = e,
            .view   = view,
            .buf    = buf,
            .now_ns = NOW_NS()
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