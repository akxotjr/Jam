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
		void AppendWireTimePatch(const PacketHeaderView& header, std::array<WireTimePatch, kMaxUdpScatterParts>& patches, size_t& patchCount)
		{
			if (header.Type() != ePacketType::SYSTEM || patchCount >= patches.size())
				return;

			BYTE* destination = nullptr;
			const auto id = ToEnum<eSystemPacketId>(header.Id());
			
			if (id == eSystemPacketId::PING && header.PayloadSize() >= sizeof(PING_DATA))
				destination = header.Payload() + offsetof(PING_DATA, t1Wire_ns);
			else if (id == eSystemPacketId::PONG && header.PayloadSize() >= sizeof(PONG_DATA))
				destination = header.Payload() + offsetof(PONG_DATA, t3Wire_ns);

			if (destination)
				patches[patchCount++] = WireTimePatch{ destination };
		}

		bool IsAckedBy(const AckData& ack, uint16 seq)
		{
			if (HasFlag(ack.header.flags, AckFlags::HasCumulativeAck) && !SeqGreater(seq, ack.header.cumulativeAck))
				return true;

			uint16 cursor = ack.header.largestAck;
			for (uint8 i = 0; i < ack.header.rangesCount; ++i)
			{
				const AckRange& range = ack.ranges[i];
				cursor = static_cast<uint16>(cursor - range.gap);

				if (SeqDistance(cursor, seq) < range.length)
					return true;
				
				cursor = static_cast<uint16>(cursor - range.length);
			}
			return false;
		}

		bool IsValidAck(const AckData& ack)
		{
			if ((ack.header.flags & ~AckFlags::HasCumulativeAck) != 0 || ack.header.rangesCount > kMaxAckRanges)
				return false;

			if (ack.header.rangesCount == 0)
				return HasFlag(ack.header.flags, AckFlags::HasCumulativeAck) && ack.header.largestAck == ack.header.cumulativeAck;

			uint32 coveredDistance = 0;
			const uint32 distanceLimit = HasFlag(ack.header.flags, AckFlags::HasCumulativeAck) ? std::min<uint16>(SeqDistance(ack.header.largestAck, ack.header.cumulativeAck), ReliabilityState::AckTrackSize) : ReliabilityState::AckTrackSize;

			for (uint8 i = 0; i < ack.header.rangesCount; ++i)
			{
				const AckRange& range = ack.ranges[i];
				if (range.length == 0 || (i == 0 && range.gap != 0))
					return false;

				coveredDistance += static_cast<uint32>(range.gap) + range.length;
				if (coveredDistance > distanceLimit)
					return false;
			}
			return true;
		}

		bool HasSameAckCoverage(const AckData& lhs, const AckData& rhs)
		{
			AckHeader lhsHeader = lhs.header;
			AckHeader rhsHeader = rhs.header;
			lhsHeader.ackDelay_us = 0;
			rhsHeader.ackDelay_us = 0;

			return std::memcmp(&lhsHeader, &rhsHeader, sizeof(AckHeader)) == 0 && std::memcmp(lhs.ranges, rhs.ranges, sizeof(AckRange) * lhs.header.rangesCount) == 0;
		}

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

		void AppendPacketToChain(PacketChain& dst, const Packet& packet)
		{
			if (!packet.IsValid())
				return;

			dst.Add(packet);
		}

		void MarkRetransmitSubmitted(NetworkMetrics& metrics, ReliabilityState::PendingPacket& pkt, const Timestamp now)
		{
			pkt.lastTransmitTime = now;
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

		void ProcessAck(ShardLocal& L, const entt::entity e, const AckData& ack, const Timestamp ackRecvTime)
		{
			auto& R = L.registry;
			auto* rel = R.try_get<ReliabilityState>(e);
			auto* rtt = R.try_get<RttState>(e);
			if (!rel || !rtt) return;

			struct AckedMeta
			{
				bool   hasRetransmitted = false;
				uint32 size				= 0;
			};

			std::vector<AckedMeta> acked;
			acked.reserve(rel->reliablePendings.size());
			for (const auto& [seq, pkt] : rel->reliablePendings)
			{
				if (IsAckedBy(ack, seq))
					acked.push_back(AckedMeta{
						.hasRetransmitted = pkt.hasRetransmitted,
						.size			  = pkt.packet.IsValid() ? pkt.packet->Size() : 0
					});
			}

			rel->ProcessAck(ack, ackRecvTime, *rtt);
			auto& metrics = L.networkMetrics;
			metrics.RemoveReliablePending(static_cast<uint32>(acked.size()));

			for (const AckedMeta& meta : acked)
				metrics.RecordReliableAcked(meta.size, meta.hasRetransmitted);
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
						.now         = NOW(),
						.ingressTime	= pkt.recvTime,
						.orderedSpan    = pkt.span
					};

					DispatchApplicationPacket(bufferedCtx);
				}
			}

		}



	}



	void BootstrapSessionEntity(ShardLocal& L, entt::entity e, Session* session)
	{
		auto& R = L.registry;
		Timestamp now = NOW();

		// common component
		if (!R.all_of<SessionInfo>(e))                    R.emplace<SessionInfo>(e, SessionInfo::FromSession(session, now));
		if (!R.all_of<TransmissionWaitingQueue>(e))       R.emplace<TransmissionWaitingQueue>(e);
		if (!R.all_of<RpcState>(e))                       R.emplace<RpcState>(e);


		// UDP-only component
		if (session->IsUdp())
		{
			if (!R.all_of<TimeSyncState>(e))
			{
				auto& ts = R.emplace<TimeSyncState>(e);
				ts.bIsServerSide        = session->IsServerSide();
				ts.lastPingSend			= now;
				ts.lastPongRecv			= now;
				ts.lastT2ServerRecv_ns  = now.Ns();
				ts.lastT4ClientRecv_ns  = now.Ns();
			}

			if (!R.all_of<SequenceState>(e))    R.emplace<SequenceState>(e);
			if (!R.all_of<OrderState>(e))       R.emplace<OrderState>(e);
			if (!R.all_of<RttState>(e))         R.emplace<RttState>(e);
			if (!R.all_of<ReliabilityState>(e)) R.emplace<ReliabilityState>(e);
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

			if (!orderState->StoreRecvPacket(orderSeq, orderedSpan, ctx.packet, ctx.now))
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

		if (reliability->hasReliabilityRecv)
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
					reliability->MarkAckPending(ctx.ingressTime);
					ctx.shouldDrop = true;
					ctx.L.networkMetrics.RecordDuplicate();
					return false;
				}
			}
		}

		reliability->MarkReceived(reliabilitySeq, ctx.ingressTime);
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

		const Timestamp now = ctx.now;
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
		auto* rtt       = R.try_get<RttState>(ctx.e);
		auto* info      = R.try_get<SessionInfo>(ctx.e);
		if (!info)
			return false;

		const Timestamp now = ctx.now;

		switch (U2E(eSystemPacketId, ctx.view.Id()))
		{

		case eSystemPacketId::PING:
		{
			if (!timesync || !rtt)
				return true;
			if (ctx.view.PayloadSize() < sizeof(PING_DATA))
				return true;

			const auto* ping = reinterpret_cast<const PING_DATA*>(ctx.view.Payload());

			timesync->lastT1ClientSend_ns = ping->t1App_ns;
			timesync->lastT2ServerRecv_ns = now.Ns();
			timesync->lastSampleClient_ns = ping->t1App_ns;

			PONG_DATA pong{};

			pong.t1Wire_ns = ping->t1Wire_ns;
			pong.t2Wire_ns = ctx.ingressTime.Ns(); // wire ingress
			pong.t3Wire_ns = 0;                  // wire egress : fill in UdpRouter

			pong.t1App_ns = ping->t1App_ns;
			pong.t2App_ns = now.Ns();
			pong.t3App_ns = NOW().Ns();

			if (auto* txQueue = R.try_get<TransmissionWaitingQueue>(ctx.e))
			{
				txQueue->Enqueue(PacketBuilder::CreatePongPacket(pong));
			}
			return true;
		}

		case eSystemPacketId::PONG:
		{
			if (!timesync || !rtt)
				return true;
			if (ctx.view.PayloadSize() < sizeof(PONG_DATA))
				return true;

			const auto*  pong = reinterpret_cast<const PONG_DATA*>(ctx.view.Payload());
			if (pong->t1App_ns < timesync->lastT1ClientSend_ns)
				return true;

			const Timestamp t4_app  = now;
			const Timestamp t4_wire = ctx.ingressTime;
			const bool hasWireTimes = pong->t1Wire_ns != 0
				&& pong->t2Wire_ns != 0
				&& pong->t3Wire_ns != 0
				&& t4_wire.Ns() >= pong->t1Wire_ns
				&& pong->t3Wire_ns >= pong->t2Wire_ns;

			timesync->lastPongRecv		  = t4_app;
			timesync->lastT2ServerRecv_ns = pong->t2App_ns;
			timesync->lastT3ServerSend_ns = pong->t3App_ns;
			timesync->lastT4ClientRecv_ns = t4_app.Ns();

			if (hasWireTimes)
			{
				timesync->ProcessPingPong(pong->t1Wire_ns, pong->t2Wire_ns, pong->t3Wire_ns, t4_wire.Ns(), *rtt);
			}
			else
			{
				timesync->ProcessPingPong(pong->t1App_ns, pong->t2App_ns, pong->t3App_ns, t4_app.Ns(), *rtt);
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
			if (ctx.view.PayloadSize() < sizeof(AckHeader))
				return;

			AckData ack{};
			std::memcpy(&ack.header, ctx.view.Payload(), sizeof(AckHeader));
			if (ack.header.rangesCount > kMaxAckRanges)
				return;

			const uint32 expectedSize = static_cast<uint32>(sizeof(AckHeader))
				+ static_cast<uint32>(sizeof(AckRange)) * ack.header.rangesCount;
			if (ctx.view.PayloadSize() != expectedSize)
				return;

			if (ack.header.rangesCount != 0)
				std::memcpy(ack.ranges, ctx.view.Payload() + sizeof(AckHeader),
					sizeof(AckRange) * ack.header.rangesCount);
			if (!IsValidAck(ack))
				return;

			ProcessAck(ctx.L, ctx.e, ack, ctx.ingressTime);
		}
	}



	void PipelineIncomingPacket(RecvContext& ctx)
	{
		auto& R = ctx.L.registry;
		auto& sessInfo = R.get<SessionInfo>(ctx.e);
		sessInfo.lastRecvTime = ctx.now;

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


			txQueue.Enqueue(frag);
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
			txQueue->Enqueue(ctx.packet);
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
		txQueue->Enqueue(ctx.packet);
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
			txQueue->Enqueue(ctx.packet);
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


	void SystemSessionTimeout(ShardLocal& L, Timestamp now, Duration dt)
	{
		(void)dt;

		auto& R = L.registry;
		auto view = R.view<SessionInfo, TimeSyncState>();

		for (auto entity : view)
		{
			auto& info      = view.get<SessionInfo>(entity);
			auto& timesync  = view.get<TimeSyncState>(entity);

			if (info.state != SessionInfo::CONNECTED)      continue;
			if (!info.session || !info.session->IsReady()) continue;

			if ((now - info.connectedTime) < 3_s)
				continue;

			const Timestamp keepaliveAliveNs = timesync.bIsServerSide ? Timestamp::FromNs(timesync.lastT2ServerRecv_ns) : timesync.lastPongRecv;
			const Timestamp lastAliveNs		 = std::max(keepaliveAliveNs, info.lastRecvTime);

			if (now <= lastAliveNs)
				continue;

			const Duration delta = now - lastAliveNs;
			if (delta <= SessionInfo::Timeout)
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

	void SystemSessionKeepalive(ShardLocal& L, Timestamp now, Duration dt)
	{
		(void)dt;

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

			if (!timeSync.ShouldSendPing(now))
				continue;

			PING_DATA ping{};
			ping.t1Wire_ns                = 0;          // wire send time : fill in UdpRouter
			ping.t1App_ns                 = now.Ns();
			ping.prev_t3_server_send_ns   = timeSync.lastT3ServerSend_ns;
			ping.prev_t4_client_recv_ns   = timeSync.lastT4ClientRecv_ns;

			txQueue.Enqueue(PacketBuilder::CreatePingPacket(ping));

			timeSync.lastPingSend		  = now;
			timeSync.lastT1ClientSend_ns  = now.Ns();
			info.lastSendTime			  = now;
		}
	}

	void SystemRpcTimeout(ShardLocal& L, Timestamp now, Duration dt)
	{
		(void)dt;

		auto& R = L.registry;
		auto view = R.view<RpcState>();

		for (auto entity : view)
		{
			auto& rpcState = view.get<RpcState>(entity);

			std::vector<uint32> timedOut = rpcState.GetTimedOutRequests(now);

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

	void SystemTransportFlush(ShardLocal& L, Timestamp now, Duration dt)
	{
		(void)dt;
		auto& registry = L.registry;
		auto view = registry.view<TransmissionWaitingQueue, SessionInfo>();

		for (auto entity : view)
		{
			auto& txQueue = view.get<TransmissionWaitingQueue>(entity);
			auto& info	  = view.get<SessionInfo>(entity);

			if (!info.session || info.session->IsClosing()) continue;

			const bool isUDP = info.session->IsUdp();

			if (!isUDP)	// tcp path
			{
				if (!txQueue.ShouldFlush(now))
					continue;

				Stream stream;
				stream.Reserve(txQueue.queue.size());
				for (const auto& pkt : txQueue.queue)
				{
					if (!pkt.packet.IsValid())
						continue;

					const auto header = PacketHeaderView::Parse(pkt.packet->Head(), pkt.packet->Size());
					if (!header.IsValid())
						continue;

					L.networkMetrics.RecordTransmit(pkt.packet->Size(), false, false, false, false, false);
					stream.Add(pkt.packet);
				}

				txQueue.Clear();
				if (!stream.Empty())
				{
					static_cast<TcpSession*>(info.session)->RegisterSend(std::move(stream));
					txQueue.lastFlushTime = now;
					info.lastSendTime	 = now;
				}
				continue;
			}

			auto* reliability = registry.try_get<ReliabilityState>(entity);
			if (!reliability)
			{
				continue;
			}

			const bool ackDue   = reliability->ShouldSendAck(NOW());

			if (!txQueue.ShouldFlush(now) && !ackDue) continue;

			AckData snapshot = reliability->ackDirty ? reliability->pendingAckData : AckData{};
			if (reliability->ackDirty)
			{
				snapshot.header.ackDelay_us = now >= reliability->firstPendingAckTime ? (now - reliability->firstPendingAckTime).Us32() : 0;
			}

			const uint32 ackCount = reliability ? reliability->pendingAckPacketCount : 0;
			bool ackPending = reliability && reliability->ackDirty;

			auto consumeAck = [&]()
			{
				if (reliability->ackDirty && reliability->pendingAckPacketCount == ackCount && HasSameAckCoverage(reliability->pendingAckData, snapshot))
					reliability->ClearPendingAck();
				ackPending = false;
			};

			auto* udp = static_cast<UdpSession*>(info.session);
			size_t remainingCount = txQueue.queue.size();
			while (remainingCount != 0)
			{
				Datagram datagram;
				std::array<WireTimePatch, kMaxUdpScatterParts> wireTimePatches = {};
				size_t wireTimePatchCount = 0;
				size_t selectedCount = 0;
				bool bundledAck = false;

				while (remainingCount != 0)
				{
					const auto& front = txQueue.queue.front();
					const auto header = front.packet.IsValid() ? PacketHeaderView::Parse(front.packet->Head(), front.packet->Size()) : PacketHeaderView{};

					if (!header.IsValid() || (header.IsReliable() && (!reliability || !reliability->TryGetPending(header.ReliabilitySequence()))))
					{
						txQueue.bytesQueued -= front.size;
						txQueue.queue.pop_front();
						--remainingCount;
						continue;
					}

					if (selectedCount != 0 && (datagram.TotalSize() + front.packet->Size() > JAMNET_MTU || datagram.Count() + 1 > kMaxUdpScatterParts))
					{
						break;
					}


					AppendPacketToChain(datagram, front.packet);
					AppendWireTimePatch(header, wireTimePatches, wireTimePatchCount);
					// Keep the current datagram's packets at the tail until submission succeeds.
					txQueue.queue.push_back(std::move(txQueue.queue.front()));
					txQueue.queue.pop_front();
					++selectedCount;
					--remainingCount;
				}

				if (selectedCount == 0) continue;

				Packet ackPacket;
				if (ackPending)
				{
					ackPacket = PacketBuilder::CreateAckPacket(snapshot.header, snapshot.ranges);
					if (ackPacket.IsValid() && datagram.TotalSize() + ackPacket->Size() <= JAMNET_MTU && datagram.Count() + 1 <= kMaxUdpScatterParts)
					{
						AppendPacketToChain(datagram, ackPacket);
						bundledAck = true;
					}
				}

				// On failure, leave these packets queued and continue with the original entries.
				Timestamp submitTime = Timestamp::Zero();
				const std::span patches(wireTimePatches.data(), wireTimePatchCount);

				if (!udp->RegisterSend(std::move(datagram), patches, &submitTime)) continue;

				for (size_t i = 0; i < selectedCount; ++i)
				{
					const auto& pkt = txQueue.queue[txQueue.queue.size() - selectedCount + i];
					const auto header = PacketHeaderView::Parse(pkt.packet->Head(), pkt.packet->Size());
					auto* pending = header.IsReliable() ? reliability->TryGetPending(header.ReliabilitySequence()) : nullptr;
					const bool retransmit = pending && pending->hasInitialSend;

					L.networkMetrics.RecordTransmit(pkt.packet->Size(), true, pending && !retransmit, retransmit, header.Type() == ePacketType::ACK, false);

					if (pending)
					{
						if (retransmit) MarkRetransmitSubmitted(L.networkMetrics, *pending, submitTime);
						else reliability->MarkInitialSend(header.ReliabilitySequence(), submitTime);
					}
				}

				for (size_t i = 0; i < selectedCount; ++i)
				{
					txQueue.bytesQueued -= txQueue.queue.back().size;
					txQueue.queue.pop_back();
				}

				if (bundledAck)
				{
					L.networkMetrics.RecordTransmit(ackPacket->Size(), true, false, false, true, false);
					consumeAck();
				}


				txQueue.lastFlushTime = now;
				info.lastSendTime	 = now;
			}

			if (ackPending && ackDue)
			{
				auto packet = PacketBuilder::CreateAckPacket(snapshot.header, snapshot.ranges);
				if (packet.IsValid() && udp->RegisterSend(packet))
				{
					L.networkMetrics.RecordTransmit(packet->Size(), true, false, false, true, false);
					consumeAck();
					txQueue.lastFlushTime = now;
					info.lastSendTime	 = now;
				}
			}

			txQueue.flushRequested = !txQueue.queue.empty();
		}
	}



	void SystemRetransmit(ShardLocal& L, Timestamp now_ns, Duration dt)
	{
		(void)now_ns;
		(void)dt;
		// TransportFlushSystem runs earlier in the same tick and records a later
		// WSASendTo submit timestamp, so retransmission checks need a fresh clock value.
		const Timestamp retransmitNow_ns = NOW();

		auto& R = L.registry;
		auto view = R.view<ReliabilityState, RttState, TransmissionWaitingQueue, SessionInfo>();

		for (auto entity : view)
		{
			auto& reliability = view.get<ReliabilityState>(entity);
			auto& rtt         = view.get<RttState>(entity);
			auto& txQueue     = view.get<TransmissionWaitingQueue>(entity);
			auto& info        = view.get<SessionInfo>(entity);
			auto& metrics     = L.networkMetrics;

			if (info.state != SessionInfo::CONNECTED)
				continue;

			auto retransmitList = reliability.GetRetransmitNeeded(retransmitNow_ns, rtt);
			for (uint16 seq : retransmitList)
			{
				auto* pending = reliability.TryGetPending(seq);
				if (!pending || !pending->packet.IsValid())
					continue;

				if (retransmitNow_ns >= pending->sendTime && retransmitNow_ns - pending->sendTime >= ReliabilityState::ReliableDeliveryTimeout)
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
				txQueue.Enqueue(pending->packet);
				pending->retransmitQueued = true;
				if (!fastRetransmit)
					metrics.RecordRetransmitTimeout();
			}
		}
	}

	void SystemFragmentCleanup(ShardLocal& L, Timestamp now, Duration dt)
	{
		auto& R = L.registry;
		auto view = R.view<FragmentState>();

		for (auto entity : view)
		{
			auto& fragState = view.get<FragmentState>(entity);
			fragState.CleanupTimeouts(now);

		}
	}


	void SystemNetworkStats(ShardLocal& L, Timestamp now, Duration dt)
	{
		(void)dt;

		auto& R = L.registry;

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
		L.networkMetrics.UpdateWindow(now, *aggregator);
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
			.now_ns     = NOW(),
		};

		PipelineOutgoingPacket(ctx);
		return true;
	}

	void ProcessReceivedPacket(entt::entity e, Packet packet, Timestamp ingressRecvTime)
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

		const Timestamp now = NOW();
		if (ingressRecvTime.IsZero())
			ingressRecvTime = now;

		if (sessionInfo->session->IsTcp())
		{
			PacketHeaderView view = PacketHeaderView::Parse(packet->Head(), packet->Size());

			if (!view.IsValid() || view.TotalSize() != packet->Size())
			{
				JAM_LOG_WARN_LOC("Invalid TCP packet");
				return;
			}

			sessionInfo->lastRecvTime = now;
			L.networkMetrics.RecordReceive(view.TotalSize(), false);

			RecvContext ctx{
				.L = L,
				.e = e,
				.view = view,
				.packet = std::move(packet),
				.now = now,
				.ingressTime = ingressRecvTime
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
				.L           = L,
				.e           = e,
				.view        = view,
				.packet      = std::move(subPacket),
				.now      = now,
				.ingressTime = ingressRecvTime
			};

			PipelineIncomingPacket(ctx);
			offset += view.TotalSize();
		}
	}

	struct NetworkDomainRegisteredTag {};

	void RegisterNetworkDomain(ShardLocal& L, Duration tickPeriod)
	{
		auto& R = L.registry;
		if (R.ctx().contains<NetworkDomainRegisteredTag>())
			return;

		auto& group = L.domainGroups[{DOMAIN_NETWORK, 0}];
		group.tag        = { .domain = DOMAIN_NETWORK, .subType = 0 };
		group.tickPeriod = tickPeriod;

		// Systems
		group.systems.emplace_back(SystemSessionTimeout);
		group.systems.emplace_back(SystemSessionKeepalive);
		group.systems.emplace_back(SystemRpcTimeout);
		group.systems.emplace_back(SystemTransportFlush);
		group.systems.emplace_back(SystemRetransmit);
		group.systems.emplace_back(SystemFragmentCleanup);
		group.systems.emplace_back(SystemNetworkStats);

		const Timestamp now = NOW();

		SystemSessionTimeout(L, now, 0_ns);
		SystemSessionKeepalive(L, now, 0_ns);
		SystemRpcTimeout(L, now, 0_ns);
		SystemTransportFlush(L, now, 0_ns);
		SystemRetransmit(L, now, 0_ns);
		SystemFragmentCleanup(L, now, 0_ns);
		SystemNetworkStats(L, now, 0_ns);

		group.entityFilter = [](const entt::registry& R, entt::entity e) {
				return R.all_of<SessionInfo>(e);
			};

		R.ctx().emplace<NetworkDomainRegisteredTag>();
	}

} // namespace jam::net
