#include "pch.h"

#include "jamnet/core/executor/GlobalExecutor.h"
#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/core/executor/ShardExecutor.h"

#include "jamnet/core/net/Session.h"
#include "jamnet/core/net/TcpSession.h"
#include "jamnet/core/net/UdpSession.h"

#include "jamnet/core/net/SessionSystems.h"

#include "jambase/EnumUtils.h"
#include "jamnet/runtime/protocol/schema/RPCSchemaIds.h"


namespace jam::net
{

	namespace
	{
		constexpr size_t kMaxUdpScatterParts                  = 16;

		void OnReliablePendingAdded(ShardLocal& L)
		{
			L.networkMetrics.RecordReliablePendingAdded();
		}

		PacketChain MakeSinglePacketChain(const Packet& packet)
		{
			PacketChain chain;
			if (packet.IsValid())
				chain.Add(packet);
			return chain;
		}

		void AppendChainParts(PacketChain& dst, const PacketChain& src)
		{
			dst.Reserve(dst.Count() + src.Count());
			for (const BufferSlice& part : src.Parts())
				dst.Add(part);
		}

		void AppendPacketToChain(PacketChain& dst, const Packet& packet)
		{
			if (!packet.IsValid())
				return;

			dst.Add(packet);
		}
		
		PacketChain BuildPiggybackAckChain(const Packet& packet, const PacketHeaderView& view, const ACK_DATA& ack)
		{
			PacketChain chain;
			if (!packet.IsValid() || !view.IsValid())
				return chain;

			PacketHeader patched = *view.Header();
			const uint16 patchedSize = static_cast<uint16>(view.TotalSize() + sizeof(ACK_DATA));
			patched.SetSize(patchedSize);
			patched.SetFlags(patched.GetFlags() | static_cast<uint8>(PacketFlags::PIGGYBACK_ACK));

			BufWriter writer(GetNetBufferPool(eNetBufferPoolKind::PiggybackAck));

			BufferSlice headerSlice = writer.OpenForPayload(view.HeaderSize(), alignof(PacketHeader));
			WritePayload(headerSlice, &patched, view.HeaderSize());
			headerSlice.Close();
			chain.Add(headerSlice);

			if (view.PayloadSize() > 0)
				chain.Add(packet->SliceVisible(view.HeaderSize(), view.PayloadSize()));

			BufferSlice ackSlice = writer.OpenForPayload(sizeof(ACK_DATA), alignof(ACK_DATA));
			WritePayload(ackSlice, &ack, sizeof(ACK_DATA));
			ackSlice.Close();
			chain.Add(ackSlice);

			return chain;
		}

		void MarkRetransmitSubmitted(NetworkMetrics& metrics, ReliabilityState::PendingPacket& pkt, const uint64 now_ns)
		{
			pkt.lastTransmitTime_ns = now_ns;
			if (pkt.retryCount < std::numeric_limits<uint8>::max())
				pkt.retryCount++;
			pkt.retransmitQueued = false;
			pkt.hasRetransmitted = true;
			pkt.fastRetransmitRequested = false;

			metrics.RecordRetransmitSubmitted(pkt.retryCount);
		}

		void MarkRetransmitGiveup(NetworkMetrics& metrics, ReliabilityState::PendingPacket& pkt)
		{
			if (pkt.countedGiveup)
				return;

			pkt.countedGiveup = true;

			if (pkt.retryCount > 0 || pkt.hasRetransmitted)
				metrics.RecordRetransmitGiveup();
		}

		void ProcessAck(ShardLocal& L, const entt::entity e, const uint16 ackBaseSeq, const uint64 ackWindow, const uint64 now_ns)
		{
			auto& R = L.registry;
			auto* reliability = R.try_get<ReliabilityState>(e);
			if (!reliability) return;

			struct AckedMeta
			{
				bool   hasRetransmitted = false;
				uint32 size        = 0;
			};

			std::array<AckedMeta, ReliabilityState::AckWindowSize + 1> acked{};
			uint32 ackedCount = 0;

			auto capture = [&](uint16 seq)
				{
					const auto* pkt = reliability->TryGetPending(seq);
					if (!pkt || ackedCount >= acked.size())
						return;

					acked[ackedCount++] = AckedMeta{
						.hasRetransmitted      = pkt->hasRetransmitted,
						.size                  = pkt->packet.IsValid() ? pkt->packet->Size() : 0
					};
				};

			capture(ackBaseSeq);
			for (uint16 i = 1; i <= ReliabilityState::AckWindowSize; ++i)
			{
				if (ackWindow & (uint64{ 1 } << (i - 1)))
					capture(static_cast<uint16>(ackBaseSeq - i));
			}

			reliability->ProcessAck(ackBaseSeq, ackWindow, now_ns);
			auto& metrics = L.networkMetrics;
			metrics.RemoveReliablePending(ackedCount);

			uint32 ackedBytes = 0;
			{
				for (uint32 i = 0; i < ackedCount; ++i)
				{
					metrics.RecordReliableAcked(acked[i].size, acked[i].hasRetransmitted);

					ackedBytes += acked[i].size;
				}
			}

			if (ackedBytes > 0)
			{
				if (auto* congestion = R.try_get<CongestionState>(e))
					congestion->OnAck(ackedBytes);
			}
		}

		void DispatchApplicationPacket(RecvContext& ctx)
		{
			auto& R = ctx.L.registry;
			auto& sessInfo = R.get<SessionInfo>(ctx.e);
			switch (ctx.view.Type())
			{
			case ePacketType::RPC:
				RPC::HandleIncomingPacket(R, ctx.e, ctx.view, ctx.packet);
				return;

			case ePacketType::CUSTOM:
				if (sessInfo.session)
					sessInfo.session->HandleCustomPacket(std::move(ctx.packet));
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
				auto buffered = orderState->PopOrderedPackets(seqState->expectedOrderSeq);
				if (buffered.empty())
					break;
				for (auto& pkt : buffered)
				{
					if (!pkt.packet.IsValid())
						continue;

					PacketHeaderView view = PacketHeaderView::Parse(pkt.packet->Head(), pkt.packet->Size());
					if (!view.IsValid())
					{
						JAM_LOG_WARN("[Ordering] Buffered packet became invalid");
						continue;
					}

					RecvContext bufferedCtx{
						.L              = L,
						.e              = e,
						.view           = view,
						.packet         = pkt.packet,
						.now_ns         = NOW_NS(),
						.ingressTime_ns = pkt.recvTime_ns,
						.orderedSpan    = pkt.span
					};

					DispatchApplicationPacket(bufferedCtx);
				}
			}

		}


		bool TryPiggybackAck(ShardLocal& L, const entt::entity e, TxPendingPacket& pending)
		{
			auto& R = L.registry;
			auto* reliability = R.try_get<ReliabilityState>(e);
			if (!reliability || !reliability->ackDirty || pending.priority != TxPriority::NORMAL)
				return false;

			if (!pending.packet.IsValid() || !pending.wire.Empty())
				return false;

			PacketHeaderView view = PacketHeaderView::Parse(pending.packet->Head(), pending.packet->Size());
			if (!view.IsValid() || view.IsFragmented())
				return false;
			if (view.TotalSize() + sizeof(ACK_DATA) > JAMNET_MTU)
				return false;

			pending.wire = BuildPiggybackAckChain(pending.packet, view, ACK_DATA{ reliability->pendingAckSeq, reliability->pendingAckWindow });
			if (pending.wire.Empty())
				return false;

			pending.size = pending.wire.TotalSize();
			reliability->ClearPendingAck();
			return true;
		}


		bool IncomingTailAckProcess(RecvContext& ctx)
		{
			if (!HasFlag(ctx.view.Flags(), PacketFlags::PIGGYBACK_ACK))
				return true;

			const uint32 payloadSize = ctx.view.PayloadSize();
			if (payloadSize < sizeof(ACK_DATA))
				return false;

			const BYTE* tail = ctx.view.Payload() + (payloadSize - sizeof(ACK_DATA));
			const auto* ack  = reinterpret_cast<const ACK_DATA*>(tail);

			ProcessAck(ctx.L, ctx.e, ack->ackBaseSeq, ack->ackWindow, ctx.now_ns);

			ctx.view.Header()->SetFlags(ClearFlag(ctx.view.Header()->GetFlags(), PacketFlags::PIGGYBACK_ACK));
			ctx.view.Header()->SetSize(static_cast<uint16>(ctx.view.TotalSize() - sizeof(ACK_DATA)));
			ctx.view.WriteHeaderTo(ctx.packet);
			ctx.packet.Get().tail = ctx.packet->head + ctx.view.Header()->GetSize();
			ctx.view = PacketHeaderView::Parse(ctx.packet->Head(), ctx.packet->Size());

			return ctx.view.IsValid();
		}


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



	void BootstrapSessionEntity(ShardLocal& L, entt::entity e, Session* session)
	{
		auto& R = L.registry;
		uint64 now_ns = NOW_NS();

		// common component
		if (!R.all_of<SessionInfo>(e))                    R.emplace<SessionInfo>(e, SessionInfo::FromSession(session, now_ns));
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

			if (!R.all_of<SequenceState>(e))    R.emplace<SequenceState>(e);
			if (!R.all_of<OrderState>(e))       R.emplace<OrderState>(e);
			if (!R.all_of<ReliabilityState>(e)) R.emplace<ReliabilityState>(e);
			if (!R.all_of<CongestionState>(e))  R.emplace<CongestionState>(e);
			if (!R.all_of<FragmentState>(e))    R.emplace<FragmentState>(e);

		}


	}

	// ============================================================
	// Packet Processing - Incoming
	// ============================================================


	bool IncomingSequencingProcess(RecvContext& ctx)
	{
		auto& R        = ctx.L.registry;
		auto* sequence = R.try_get<SequenceState>(ctx.e);
		if (!sequence) return true;

		const eChannel ch  = ctx.view.Channel();
		const uint16 recencySeq = ctx.view.RecencySequence();

		if (ch == eChannel::UNRELIABLE_SEQUENCED)
		{
			if (!sequence->IsNewerRecency(recencySeq))
			{
				ctx.shouldDrop = true;
				return false;
			}
			sequence->UpdateLatestRecency(recencySeq);
		}

		return true;
	}

	bool IncomingOrderingProcess(RecvContext& ctx)
	{
		auto& R = ctx.L.registry;
		const eChannel ch = ctx.view.Channel();

		if (ch != eChannel::RELIABLE_ORDERED)
			return true;

		auto* seqState   = R.try_get<SequenceState>(ctx.e);
		auto* orderState = R.try_get<OrderState>(ctx.e);
		if (!seqState || !orderState)
			return false;

		const uint16 orderSeq    = ctx.view.OrderSequence();
		const uint16 orderedSpan = std::max<uint16>(1, ctx.orderedSpan);
		const uint16 expectedSeq = seqState->expectedOrderSeq;

		if (orderSeq == expectedSeq)
		{
			seqState->expectedOrderSeq = static_cast<uint16>(expectedSeq + orderedSpan);
			ctx.flushOrderedPending = true;
			return true;
		}

		if (SeqGreater(orderSeq, expectedSeq))
		{
			ctx.L.networkMetrics.RecordOutOfOrder();

			if (orderState->pendings.size() >= OrderState::MaxRecvBufferSize)
			{
				JAM_LOG_WARN("[Ordering] Recv buffer overflow");
				ctx.shouldDrop = true;
				return false;
			}

			if (!orderState->StoreRecvPacket(orderSeq, orderedSpan, ctx.packet, ctx.now_ns))
			{
				ctx.shouldDrop = true;
				ctx.L.networkMetrics.RecordDuplicate();
				return false;
			}
			ctx.needsReordering = true;
			return false;
		}

		ctx.shouldDrop = true;
		ctx.L.networkMetrics.RecordDuplicate();

		return false;
	}

	bool IncomingReliabilityProcess(RecvContext& ctx)
	{
		auto& R = ctx.L.registry;
		const eChannel ch = ctx.view.Channel();

		if (!IsReliableChannel(ch))
			return true;

		auto* reliability = R.try_get<ReliabilityState>(ctx.e);
		if (!reliability)
			return false;

		const uint16 reliabilitySeq = ctx.view.ReliabilitySequence();

		if (!SeqGreater(reliabilitySeq, reliability->latestReliabilityRecvSeq - ReliabilityState::AckTrackSize))
			return false;

		if (!(reliability->ackTrack.none() && reliability->latestReliabilityRecvSeq == 0))
		{
			if (!SeqGreater(reliabilitySeq, reliability->latestReliabilityRecvSeq))
			{
				const uint16 dist = SeqDistance(reliability->latestReliabilityRecvSeq, reliabilitySeq);
				if (dist >= ReliabilityState::AckTrackSize)
				{
					ctx.shouldDrop = true;
					return false;
				}

				if (reliability->ackTrack.test(dist))
				{
					if (dist <= ReliabilityState::AckWindowSize)
					{
						reliability->MarkAckPending(ctx.now_ns);
					}
					else if (auto* txQueue = R.try_get<TransmissionWaitingQueue>(ctx.e))
					{
						txQueue->Enqueue(PacketBuilder::CreateAckPacket(ACK_DATA{ reliabilitySeq, 0 }), TxPriority::ACK_ONLY);
					}
					ctx.shouldDrop = true;
					ctx.L.networkMetrics.RecordDuplicate();
					return false;
				}
			}
		}

		reliability->MarkReceived(reliabilitySeq, ctx.now_ns);
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
		const uint16 fragmentId = IsReliableChannel(ctx.view.Channel())
			? static_cast<uint16>(ctx.view.ReliabilitySequence() - fragIdx)
			: static_cast<uint16>(ctx.view.RecencySequence() - fragIdx);

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

		JAM_LOG_DEBUG("[Fragment] Reassembly complete: id={}, size={}", fragmentId, reassembled->size());

		if (ctx.view.Channel() == eChannel::RELIABLE_ORDERED)
			ctx.orderedSpan = std::max<uint16>(1, fragTotal);

		const uint16 orderedBaseSeq = (ctx.view.Channel() == eChannel::RELIABLE_ORDERED)
			? static_cast<uint16>(ctx.view.OrderSequence() - fragIdx)
			: 0;
		const uint16 reliableBaseSeq = IsReliableChannel(ctx.view.Channel())
			? static_cast<uint16>(ctx.view.ReliabilitySequence() - fragIdx)
			: 0;

		auto rebuilt = PacketBuilder::CreatePacket(
			ctx.view.Type(), ctx.view.Id(),
			ClearFlag(ctx.view.Flags(), PacketFlags::FRAGMENTED),
			ctx.view.Channel(),
			reassembled->data(), static_cast<uint32>(reassembled->size()),
			fragmentId,
			reliableBaseSeq,
			orderedBaseSeq);

		if (!rebuilt.IsValid()) return false;
		ctx.packet = std::move(rebuilt);
		ctx.view   = PacketHeaderView::Parse(ctx.packet->Head(), ctx.packet->Size());

		return ctx.view.IsValid();
	}

	bool IncomingNetstatProcess(RecvContext& ctx)
	{
		return true;
	}

	bool HandlePostBindSystemPacket(RecvContext& ctx)
	{
		auto& R         = ctx.L.registry;
		auto* timesync  = R.try_get<TimeSyncState>(ctx.e);
		auto* info      = R.try_get<SessionInfo>(ctx.e);
		if (!info)
			return false;

		const uint64 now_ns = ctx.now_ns;

		switch (U2E(eSystemPacketId, ctx.view.Id()))
		{

		case eSystemPacketId::PING:
		{
			if (!timesync)
				return true;
			if (ctx.view.PayloadSize() < sizeof(PING_DATA))
				return true;

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
			return true;
		}

		case eSystemPacketId::PONG:
		{
			if (!timesync)
				return true;
			if (ctx.view.PayloadSize() < sizeof(PONG_DATA))
				return true;

			const auto*  pong = reinterpret_cast<const PONG_DATA*>(ctx.view.Payload());
			if (pong->t1App_ns < timesync->lastT1ClientSend_ns)
				return true;

			const uint64 t4_app  = now_ns;
			const uint64 t4_wire = ctx.ingressTime_ns;
			const bool hasWireTimes = pong->t1Wire_ns != 0
				&& pong->t2Wire_ns != 0
				&& pong->t3Wire_ns != 0
				&& t4_wire >= pong->t1Wire_ns
				&& pong->t3Wire_ns >= pong->t2Wire_ns;

			timesync->lastPongRecv_ns     = t4_app;
			timesync->lastT2ServerRecv_ns = pong->t2App_ns;
			timesync->lastT3ServerSend_ns = pong->t3App_ns;
			timesync->lastT4ClientRecv_ns = t4_app;

			if (hasWireTimes)
			{
				timesync->ProcessPingPong(pong->t1Wire_ns, pong->t2Wire_ns, pong->t3Wire_ns, t4_wire);
			}
			else
			{
				timesync->ProcessPingPong(pong->t1App_ns, pong->t2App_ns, pong->t3App_ns, t4_app);
			}

			return true;
		}

		default:
			return false;
		}

		return true;
	}

	void HandleAckPacket(RecvContext& ctx)
	{
		eChannel ch = ctx.view.Channel();
		if (ch != eChannel::UDP_DEFAULT)
			return;

		if (ToEnum<eAckPacketId>(ctx.view.Id()) == eAckPacketId::ACK)
		{
			if (ctx.view.PayloadSize() < sizeof(ACK_DATA))
				return;

			const auto* ack = reinterpret_cast<const ACK_DATA*>(ctx.view.Payload());
			ProcessAck(ctx.L, ctx.e, ack->ackBaseSeq, ack->ackWindow, ctx.now_ns);
		}
	}



	void PipelineIncomingPacket(RecvContext& ctx)
	{
		auto& R = ctx.L.registry;
		auto& sessInfo = R.get<SessionInfo>(ctx.e);
		sessInfo.lastRecvTime_ns = ctx.now_ns;

		ctx.L.networkMetrics.RecordReceive(ctx.view.TotalSize(), sessInfo.session && sessInfo.session->IsUdp());

		switch (ctx.view.Type())
		{
		case ePacketType::SYSTEM:
			HandlePostBindSystemPacket(ctx);
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
		if (!IncomingTailAckProcess(ctx))		return;
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
		const auto ch = ctx.header.Channel();

		if (!ctx.header.IsNeedToFragmentation())
			return true;
		if (!HasRecencySequence(ch) && !HasReliabilitySequence(ch))
			return false;

		auto* seqState = ctx.L.registry.try_get<SequenceState>(ctx.e);
		if (!seqState) return false;

		const uint32 fullPayloadSize = ctx.header.PayloadSize();
		const uint16 fragTotal       = static_cast<uint16>((fullPayloadSize + FragmentState::MaxFragmentPayloadSize - 1) / FragmentState::MaxFragmentPayloadSize);
		if (fragTotal > FragmentState::MaxFragments)
			return false;

		const BYTE*  basePayload     = ctx.header.Payload();
		const uint16 baseRecencySeq = ch == eChannel::UNRELIABLE_SEQUENCED ? seqState->AllocRecencySeq(fragTotal) : 0;
		const uint16 baseReliabilitySeq = IsReliableChannel(ch) ? seqState->AllocReliabilitySeq(fragTotal) : 0;

		bool isOrdered = IsOrderedChannel(ch);
		const uint16 baseOrderSeq = isOrdered ? seqState->AllocOrderSeq(fragTotal) : 0;

		auto& txQueue     = ctx.L.registry.get<TransmissionWaitingQueue>(ctx.e);
		auto* reliability = ctx.L.registry.try_get<ReliabilityState>(ctx.e);

		if (IsReliableChannel(ch) && !reliability)
			return false;

		for (auto i = 0; i < fragTotal; ++i)
		{
			const uint32 offset = i * FragmentState::MaxFragmentPayloadSize;
			const uint32 chunk  = std::min<uint32>(FragmentState::MaxFragmentPayloadSize, fullPayloadSize - offset);
			auto frag = PacketBuilder::CreatePacket(
				ctx.header.Type(),
				ctx.header.Id(),
				ctx.header.Flags() | PacketFlags::FRAGMENTED,
				ctx.header.Channel(),
				basePayload + offset,
				chunk,
				ch == eChannel::UNRELIABLE_SEQUENCED ? static_cast<uint16>(baseRecencySeq + i) : 0,
				IsReliableChannel(ch) ? static_cast<uint16>(baseReliabilitySeq + i) : 0,
				isOrdered ? static_cast<uint16>(baseOrderSeq + i) : 0,
				static_cast<uint8>(i),
				static_cast<uint8>(fragTotal)
			);

			if (!frag.IsValid()) continue;

			if (IsReliableChannel(ch))
			{
				if (!reliability->StoreSendPacket(ch, frag, static_cast<uint16>(baseReliabilitySeq + i), ctx.now_ns))
					return false;
				OnReliablePendingAdded(ctx.L);

			}


			txQueue.Enqueue(frag, TxPriority::NORMAL);
		}

		ctx.bIsFragmentized = true;

		return true;
	}

	bool OutgoingSequencingProcess(SendContext& ctx)
	{
		auto* txQueue = ctx.L.registry.try_get<TransmissionWaitingQueue>(ctx.e);
		if (!txQueue) return false;

		const auto ch = ctx.header.Channel();
		if (ch == eChannel::UDP_DEFAULT || ch == eChannel::TCP_DEFAULT)
		{
			txQueue->Enqueue(ctx.packet, ctx.priority);
			return true;
		}

		if (auto* seqState = ctx.L.registry.try_get<SequenceState>(ctx.e))
		{
			if (ch == eChannel::UNRELIABLE_SEQUENCED)
				ctx.header.Header()->SetRecencySequence(seqState->AllocRecencySeq(1));

			if (IsReliableChannel(ch))
			{
				auto* reliability = ctx.L.registry.try_get<ReliabilityState>(ctx.e);
				if (!reliability)
					return false;

				const uint16 reliabilitySeq = seqState->AllocReliabilitySeq(1);
				ctx.header.Header()->SetReliabilitySequence(reliabilitySeq);

				if (IsOrderedChannel(ch))
				{
					const uint16 orderSeq = seqState->AllocOrderSeq(1);
					ctx.header.Header()->SetOrderSequence(orderSeq);
				}

				ctx.header.WriteHeaderTo(ctx.packet);

				if (!reliability->StoreSendPacket(ch, ctx.packet, reliabilitySeq, ctx.now_ns))
					return false;
				OnReliablePendingAdded(ctx.L);

			}
		}

		ctx.header.WriteHeaderTo(ctx.packet);
		txQueue->Enqueue(ctx.packet, ctx.priority);
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
			txQueue->Enqueue(ctx.packet, ctx.priority);
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


	void SystemSessionTimeout(ShardLocal& L, uint64 now_ns, uint64 dt_ns)
	{
		auto& R = L.registry;
		auto view = R.view<SessionInfo, TimeSyncState>();

		for (auto entity : view)
		{
			auto& info      = view.get<SessionInfo>(entity);
			auto& timesync  = view.get<TimeSyncState>(entity);

			if (info.state != SessionInfo::CONNECTED)      continue;
			if (!info.session || !info.session->IsReady()) continue;

			if ((now_ns - info.connectedTime_ns) < 3_s)
				continue;

			const uint64 keepaliveAliveNs = timesync.bIsServerSide ? timesync.lastT2ServerRecv_ns : timesync.lastPongRecv_ns;

			const uint64 lastAliveNs = std::max(keepaliveAliveNs, info.lastRecvTime_ns);

			if (now_ns <= lastAliveNs)
				continue;

			const uint64 delta = now_ns - lastAliveNs;
			if (delta <= SessionInfo::Timeout_ns)
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
		auto view = R.view<SessionInfo, TransmissionWaitingQueue, TimeSyncState>();

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

				JAM_LOG_WARN("[RPC] Request {} timed out", requestId);
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
					auto pkt = PacketBuilder::CreateAckPacket(ACK_DATA{ reliability.pendingAckSeq, reliability.pendingAckWindow });
					txQueue.Enqueue(pkt, TxPriority::ACK_ONLY);
					reliability.ClearPendingAck();
				}
			}

			if (txQueue.queue.empty())
				continue;
			if (!txQueue.ShouldFlush(now_ns))
				continue;

			std::ranges::stable_sort(txQueue.queue, [](const TxPendingPacket& a, const TxPendingPacket& b) {
					return GetPriority(a.priority) < GetPriority(b.priority);
				});

			std::vector<PacketChain> batch;
			batch.reserve(txQueue.queue.size());

			std::vector<TxPendingPacket> remain;
			remain.reserve(txQueue.queue.size());

			auto& metrics = L.networkMetrics;
			PacketChain currentUdpBundle;
			uint32 currentUdpBundleBytes = 0;
			uint32 currentUdpBundlePacketCount = 0;
			bool hasCurrentUdpBundle = false;
			bool currentUdpBundleReliable = false;

			auto flushUdpBundle = [&]()
			{
				if (!hasCurrentUdpBundle || currentUdpBundle.Empty())
					return;

				batch.push_back(std::move(currentUdpBundle));
				currentUdpBundle = {};
				currentUdpBundleBytes = 0;
				currentUdpBundlePacketCount = 0;
				hasCurrentUdpBundle = false;
				currentUdpBundleReliable = false;
			};

			for (size_t i = 0; i < txQueue.queue.size(); ++i)
			{
				auto& pkt = txQueue.queue[i];

				PacketHeaderView v = PacketHeaderView::Parse(pkt.packet->Head(), pkt.packet->Size());
				if (!v.IsValid()) continue;

				ReliabilityState::PendingPacket* pending = nullptr;
				if (isUdp && reliabilityState && v.IsReliable())
				{
					pending = reliabilityState->TryGetPending(v.ReliabilitySequence());
					if (pkt.priority == TxPriority::RETRANSMIT && !pending)
						continue;
				}

				const bool bypassCongestion = (pkt.priority <= TxPriority::ACK_ONLY);

				if (!bypassCongestion && isUdp && congestion && v.IsReliable() && !congestion->CanSend(pkt.size))
				{
					remain.insert(remain.end(), txQueue.queue.begin() + static_cast<std::ptrdiff_t>(i), txQueue.queue.end());
					break;
				}

				const bool hasPiggybackAck = !pkt.wire.Empty();
				const uint32 wireSize = hasPiggybackAck ? pkt.wire.TotalSize() : pkt.packet->Size();

				if (isUdp)
				{
					const bool packetReliable = v.IsReliable();
					if (wireSize > JAMNET_MTU)
					{
						flushUdpBundle();
						batch.push_back(hasPiggybackAck ? std::move(pkt.wire) : MakeSinglePacketChain(pkt.packet));
					}
					else
					{
						const size_t wirePartCount = hasPiggybackAck ? pkt.wire.Count() : 1;
						if (hasCurrentUdpBundle
							&& (currentUdpBundleReliable != packetReliable
								|| (currentUdpBundleBytes + wireSize) > JAMNET_MTU
								|| (currentUdpBundle.Count() + wirePartCount) > kMaxUdpScatterParts))
						{
							flushUdpBundle();
						}

						if (!hasCurrentUdpBundle)
						{
							hasCurrentUdpBundle = true;
							currentUdpBundleReliable = packetReliable;
							currentUdpBundleBytes = 0;
							currentUdpBundlePacketCount = 0;
						}

						if (hasPiggybackAck)
							AppendChainParts(currentUdpBundle, pkt.wire);
						else
							AppendPacketToChain(currentUdpBundle, pkt.packet);

						currentUdpBundleBytes += wireSize;
						currentUdpBundlePacketCount++;
					}
				}
				else
				{
					batch.push_back(hasPiggybackAck ? std::move(pkt.wire) : MakeSinglePacketChain(pkt.packet));
				}

				const bool countedReliableOriginal = (pending != nullptr && pkt.priority != TxPriority::RETRANSMIT);
				metrics.RecordTransmit(pkt.size, isUdp, countedReliableOriginal,
					pkt.priority == TxPriority::RETRANSMIT,
					v.Type() == ePacketType::ACK && pkt.priority == TxPriority::ACK_ONLY,
					hasPiggybackAck || HasFlag(v.Flags(), PacketFlags::PIGGYBACK_ACK));

				if (pending)
				{
					if (pkt.priority == TxPriority::RETRANSMIT)
						MarkRetransmitSubmitted(metrics, *pending, now_ns);
					else if (!pending->hasInitialSend)
					{
						pending->hasInitialSend      = true;
						pending->sendTime_ns         = now_ns;
						pending->lastTransmitTime_ns = now_ns;
					}
				}

				if (!bypassCongestion && isUdp && congestion && v.IsReliable())
					congestion->OnSend(pkt.size);
			}

			if (isUdp)
				flushUdpBundle();

			txQueue.queue       = std::move(remain);
			txQueue.bytesQueued = 0;
			for (const auto& p : txQueue.queue) txQueue.bytesQueued += p.size;
			txQueue.flushRequested = !txQueue.queue.empty();

			if (batch.empty())
				continue;

			Session* session = info.session;
			if (session->IsUdp())
			{
				auto* udp = static_cast<UdpSession*>(session);
				udp->RegisterSend(std::move(batch));
			}
			else if (session->IsTcp())
			{
				auto* tcp = static_cast<TcpSession*>(session);
				tcp->RegisterSend(std::move(batch));
			}

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
			auto& metrics     = L.networkMetrics;

			if (info.state != SessionInfo::CONNECTED)
				continue;

			auto retransmitList = reliability.GetRetransmitNeeded(now_ns);
			for (uint16 seq : retransmitList)
			{
				auto* pending = reliability.TryGetPending(seq);
				if (!pending || !pending->packet.IsValid())
					continue;

				if (now_ns - pending->sendTime_ns >= ReliabilityState::ReliableDeliveryTimeout_ns)
				{
					JAM_LOG_ERROR("[Retransmit] Packet seq={} exceeded reliable delivery timeout", seq);
					MarkRetransmitGiveup(metrics, *pending);
					info.state = SessionInfo::DISCONNECTING;
					L.defers.emplace_back([entity](entt::registry& rr)
						{
							if (!rr.valid(entity)) return;
							if (auto* sessionInfo = rr.try_get<SessionInfo>(entity); sessionInfo && sessionInfo->session)
								sessionInfo->session->Disconnect();
						});
					break;
				}

				const bool fastRetransmit = pending->fastRetransmitRequested;
				txQueue.Enqueue(pending->packet, TransmissionWaitingQueue::RETRANSMIT);
				pending->retransmitQueued = true;
				if (!fastRetransmit)
					metrics.RecordRetransmitTimeout();
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


	void SystemNetworkStats(ShardLocal& L, uint64 now_ns, uint64 dt_ns)
	{
		auto& R = L.registry;
		const uint64 sampleTime_ns = (now_ns > 0) ? now_ns : NOW_NS();
		(void)dt_ns;

		auto* aggregator = GLOBAL_EXEC.GetMetricsAggregator();
		if (!aggregator || !aggregator->IsEnabled())
			return;

		uint64 currentSessions = 0;
		uint64 currentPendingReliable = 0;
		auto view = R.view<SessionInfo>();
		for (auto entity : view)
		{
			++currentSessions;
			const auto* reliability = R.try_get<ReliabilityState>(entity);
			currentPendingReliable += reliability ? reliability->reliablePendings.size() : 0;
		}

		L.networkMetrics.SetCurrent(currentSessions, currentPendingReliable);
		L.networkMetrics.UpdateWindow(sampleTime_ns, *aggregator);
	}


	// ============================================================
	// Helper Functions
	// ============================================================


	bool SendPacketToSession(entt::entity e, Packet packet)
	{
		auto& L = CurrentShardLocalChecked();
		auto& R = L.registry;
		if (!R.valid(e))
		{
			JAM_LOG_WARN_LOC("Invalid entity");
			return false;
		}

		if (!R.all_of<SessionInfo>(e))
		{
			JAM_LOG_WARN_LOC("Entity doesn't have SessionInfo");
			return false;
		}

		PacketHeaderView view = PacketHeaderView::Parse(packet->Head(), packet->Size());

		if (!view.IsValid())
		{
			JAM_LOG_WARN_LOC("Invalid Packet View");
			return false;
		}

		SendContext ctx{
			.L          = L,
			.e          = e,
			.header     = view,
			.packet     = packet,
			.priority   = TransmissionWaitingQueue::NORMAL,
			.now_ns     = NOW_NS(),
		};

		PipelineOutgoingPacket(ctx);
		return true;
	}

	void ProcessReceivedPacket(entt::entity e, Packet packet, uint64 ingressRecvTime_ns)
	{
		auto& L = CurrentShardLocalChecked();
		auto& R = L.registry;

		if (!R.valid(e))
		{
			JAM_LOG_WARN_LOC("Invalid Entity");
			return;
		}

		auto* sessionInfo = R.try_get<SessionInfo>(e);
		if (!sessionInfo || !sessionInfo->session)
		{
			JAM_LOG_WARN_LOC("Entity doesn't have valid SessionInfo");
			return;
		}

		const uint64 now_ns = NOW_NS();
		if (ingressRecvTime_ns == 0)
			ingressRecvTime_ns = now_ns;

		if (sessionInfo->session->IsTcp())
		{
			PacketHeaderView view = PacketHeaderView::Parse(packet->Head(), packet->Size());

			if (!view.IsValid() || view.TotalSize() != packet->Size())
			{
				JAM_LOG_WARN_LOC("Invalid TCP packet");
				return;
			}

			sessionInfo->lastRecvTime_ns = now_ns;
			L.networkMetrics.RecordReceive(view.TotalSize(), false);

			RecvContext ctx{
				.L = L,
				.e = e,
				.view = view,
				.packet = std::move(packet),
				.now_ns = now_ns,
				.ingressTime_ns = ingressRecvTime_ns
			};

			DispatchApplicationPacket(ctx);
			return;
		}

		uint32 offset = 0;
		while (offset < packet->Size())
		{
			const uint32 remaining = packet->Size() - offset;
			PacketHeaderView view = PacketHeaderView::Parse(packet->Head() + offset, remaining);
			if (!view.IsValid())
			{
				JAM_LOG_WARN(
					"[Session] Invalid packet received in datagram. offset={}, remaining={}, datagramSize={}",
					offset,
					remaining,
					packet->Size());
				return;
			}

			Packet subPacket = MakeOwned(packet->SliceVisible(offset, view.TotalSize()));
			RecvContext ctx{
				.L              = L,
				.e              = e,
				.view           = view,
				.packet         = std::move(subPacket),
				.now_ns         = now_ns,
				.ingressTime_ns = ingressRecvTime_ns
			};

			PipelineIncomingPacket(ctx);
			offset += view.TotalSize();
		}
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
		group.systems.emplace_back(SystemNetworkStats);

		const uint64 now_ns = NOW_NS();

		SystemSessionTimeout(L, now_ns, 0);
		SystemSessionKeepalive(L, now_ns, 0);
		SystemRpcTimeout(L, now_ns, 0);
		SystemTransportFlush(L, now_ns, 0);
		SystemRetransmit(L, now_ns, 0);
		SystemFragmentCleanup(L, now_ns, 0);
		SystemNetworkStats(L, now_ns, 0);

		group.entityFilter = [](const entt::registry& R, entt::entity e) {
				return R.all_of<SessionInfo>(e);
			};

		R.ctx().emplace<NetworkDomainRegisteredTag>();
	}

} // namespace jam::net
