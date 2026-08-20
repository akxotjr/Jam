#include "pch.h"
#include "jamnet/core/net/SessionComponents.h"
#include "jamnet/core/net/Session.h"

namespace jam::net
{
	// ============================================================
	//	SessionInfo
	// ============================================================

	SessionInfo SessionInfo::FromSession(Session* session, uint64 now_ns)
	{
		SessionInfo comp;
		comp.session			= session;
		comp.connectedTime_ns	= now_ns;
		comp.lastRecvTime_ns	= now_ns;
		comp.lastSendTime_ns	= now_ns;
		comp.state				= State::CONNECTED;

		return comp;
	}


	// ============================================================
	//	FragmentState
	// ============================================================

	bool FragmentState::Reassembly::AddFragment(uint8 index, const BYTE* data, uint32 size, uint64 now_ns)
	{
		if (index >= totalFragments)  // invalid fragment
			return false;  

		if (receivedMask.test(index)) // already received
			return false; 

		fragments[index].assign(data, data + size);
		receivedMask.set(index);
		receivedCount++;
		lastRecvTime_ns = now_ns;

		return true;
	}

	std::vector<BYTE> FragmentState::Reassembly::Assemble() const
	{
		std::vector<BYTE> result;
		for (const auto& frag : fragments)
		{
			result.insert(result.end(), frag.begin(), frag.end());
		}
		return result;
	}


	bool FragmentState::AddFragment(uint16 fragmentId, uint8 totalFragments, const uint8 index, const BYTE* data, const uint32 size, uint64 now_ns)
	{
		auto it = reassemblies.find(fragmentId);
		if (it == reassemblies.end())
		{
			auto [newIt, inserted] = reassemblies.emplace(
				std::piecewise_construct,
				std::forward_as_tuple(fragmentId),
				std::forward_as_tuple(fragmentId, totalFragments, now_ns)
			);
			it = newIt;
		}

		return it->second.AddFragment(index, data, size, now_ns);
	}

	std::optional<std::vector<BYTE>> FragmentState::PopCompleted(const uint16 fragmentId)
	{
		const auto it = reassemblies.find(fragmentId);
		if (it == reassemblies.end())
			return std::nullopt;

		if (!it->second.IsComplete())
			return std::nullopt;

		auto result = it->second.Assemble();
		reassemblies.erase(it);
		return result;
	}

	void FragmentState::CleanupTimeouts(const uint64 now_ns)
	{
		for (auto it = reassemblies.begin(); it != reassemblies.end(); )
		{
			if (now_ns - it->second.lastRecvTime_ns > Timeout_ns)
			{
				timeoutDrops++;
				it = reassemblies.erase(it);
			}
			else
			{
				++it;
			}
		}
	}
	// ============================================================
	//  OrderState
	// ============================================================

	bool OrderState::StoreRecvPacket(uint16 orderSeq, uint16 span, ::jam::net::Packet packet, uint64 now_ns)
	{
		if (pendings.size() >= MaxRecvBufferSize)
			return false;

		if (pendings.contains(orderSeq))
			return false;

		pendings.emplace(orderSeq, OrderedPacket{
			.orderSeq    = orderSeq,
			.span		 = span,
			.recvTime_ns = now_ns,
			.packet		 = std::move(packet)
		});
		return true;
	}

	std::vector<OrderState::OrderedPacket> OrderState::PopOrderedPackets(OUT uint16& expectedSeq)
	{
		std::vector<OrderedPacket> out;
		while (true)
		{
			auto it = pendings.find(expectedSeq);
			if (it == pendings.end())
				break;

			out.push_back(it->second);
			const uint16 span = std::max<uint16>(1, it->second.span);
			pendings.erase(it);
			expectedSeq = static_cast<uint16>(expectedSeq + span);
		}
		return out;
	}


	// ============================================================
	//  ReliabilityState
	// ============================================================

	bool ReliabilityState::StoreSendPacket(eChannel ch, Packet packet, uint16 reliabilitySeq, uint64 now_ns)
	{
		if (!IsReliableChannel(ch))
			return false;

		if (reliablePendings.contains(reliabilitySeq))
			return false;

		reliablePendings.emplace(reliabilitySeq, PendingPacket{
				.reliabilitySeq	   = reliabilitySeq,
				.channel			   = ch,
				.sendTime_ns		   = now_ns,
				.lastTransmitTime_ns   = now_ns,
				.retryCount			   = 0,
				.packet				   = packet
			});

		if (packet.IsValid()) inflightSize += packet->Size();

		return true;
	}

	std::vector<uint16> ReliabilityState::GetRetransmitNeeded(uint64 now_ns) const
	{
		std::vector<uint16> out;
		for (const auto& [seq, pkt] : reliablePendings)
		{
			if (!pkt.hasInitialSend || pkt.retransmitQueued)
				continue;

			if (pkt.fastRetransmitRequested || now_ns - pkt.lastTransmitTime_ns >= GetRetransmitTimeout(pkt.retryCount))
				out.push_back(seq);
		}
		return out;
	}

	uint64 ReliabilityState::GetRetransmitTimeout(uint8 retryCount) const
	{
		const uint64 baseRto = smoothedRtt_ns == 0 ? InitialRetransmitTimeout_ns : std::clamp(smoothedRtt_ns + 4 * rttVariance_ns, MinRetransmitTimeout_ns, MaxRetransmitTimeout_ns);
		const uint8  shift   = std::min(retryCount, MaxBackoffShift);

		return std::min(baseRto << shift, MaxBackoffTimeout_ns);
	}

	ReliabilityState::PendingPacket* ReliabilityState::TryGetPending(uint16 seq)
	{
		const auto it = reliablePendings.find(seq);
		if (it == reliablePendings.end())
			return nullptr;

		return &it->second;
	}

	const ReliabilityState::PendingPacket* ReliabilityState::TryGetPending(uint16 seq) const
	{
		const auto it = reliablePendings.find(seq);
		if (it == reliablePendings.end())
			return nullptr;

		return &it->second;
	}

	void ReliabilityState::ErasePendingPacket(uint16 seq)
	{
		const auto it = reliablePendings.find(seq);
		if (it == reliablePendings.end())
			return;

		if (it->second.packet.IsValid())
		{
			const uint32 size = it->second.packet->Size();
			inflightSize = (inflightSize >= size) ? (inflightSize - size) : 0;
		}

		reliablePendings.erase(it);
	}

	void ReliabilityState::MarkReceived(uint16 seq, uint64 now_ns)
	{
		if (ackTrack.none() && latestReliabilityRecvSeq == 0)
		{
			latestReliabilityRecvSeq = seq;
			ackTrack.reset();
			ackTrack.set(0);
		}
		else if (SeqGreater(seq, latestReliabilityRecvSeq))
		{
			const uint16 advance = SeqDistance(seq, latestReliabilityRecvSeq);

			if (advance >= AckTrackSize)
				ackTrack.reset();
			else
				ackTrack <<= advance;

			latestReliabilityRecvSeq = seq;
			ackTrack.set(0);
		}
		else
		{
			const uint16 dist = SeqDistance(latestReliabilityRecvSeq, seq);
			if (dist < AckTrackSize)
				ackTrack.set(dist);
		}

		MarkAckPending(now_ns);
	}

	void ReliabilityState::MarkAckPending(uint64 now_ns)
	{
		if (!ackDirty)
		{
			ackDirty = true;
			firstPendingAckTime_ns = now_ns;
		}
		++pendingAckPacketCount;

		BuildPendingAck();
	}

	void ReliabilityState::BuildPendingAck()
	{
		if (!ackDirty)
			return;

		pendingAckSeq	   = latestReliabilityRecvSeq;
		pendingAckWindow = BuildAckWindow();
	}

	void ReliabilityState::ProcessAck(uint16 ackSeq, uint64 ackWindow, uint64 now_ns)
	{
		bool sampledRtt = false;
		auto ackOne = [&](uint16 seq)
			{
				auto it = reliablePendings.find(seq);
				if (it == reliablePendings.end())
					return;

				const auto& pending = it->second;
				if (!sampledRtt && pending.hasInitialSend && !pending.hasRetransmitted && now_ns >= pending.sendTime_ns)
				{
					const uint64 sample = now_ns - pending.sendTime_ns;
					if (smoothedRtt_ns == 0)
					{
						smoothedRtt_ns  = sample;
						rttVariance_ns = sample / 2;
					}
					else
					{
						const uint64 deviation = smoothedRtt_ns > sample ? smoothedRtt_ns - sample : sample - smoothedRtt_ns;
						rttVariance_ns = (3 * rttVariance_ns + deviation) / 4;
						smoothedRtt_ns  = (7 * smoothedRtt_ns + sample) / 8;
					}
					sampledRtt = true;
				}

				if (it->second.packet.IsValid())
				{
					const uint32 size = it->second.packet->Size();
					inflightSize = (inflightSize >= size) ? (inflightSize - size) : 0;
				}

				reliablePendings.erase(it);
			};

		ackOne(ackSeq);

		for (uint16 i = 1; i <= AckWindowSize; ++i)
		{
			if (ackWindow & (uint64{ 1 } << (i - 1)))
				ackOne(static_cast<uint16>(ackSeq - i));
		}

		for (auto& [pendingSeq, pending] : reliablePendings)
		{
			if (pending.fastRetransmitUsed || !SeqGreater(ackSeq, pendingSeq))
				continue;

			const uint16 distance = SeqDistance(ackSeq, pendingSeq);
			if (distance > AckWindowSize)
				continue;

			uint8 newerAckCount = 1; // ackSeq itself
			for (uint16 i = 1; i < distance && newerAckCount < FastRetransmitThreshold; ++i)
			{
				if (ackWindow & (uint64{ 1 } << (i - 1)))
					++newerAckCount;
			}

			if (newerAckCount >= FastRetransmitThreshold)
			{
				pending.fastRetransmitRequested = true;
				pending.fastRetransmitUsed = true;
			}
		}

		if (SeqGreater(ackSeq, lastAckedReliabilitySeq))
			lastAckedReliabilitySeq = ackSeq;
	}

	bool ReliabilityState::ShouldSendAck(uint64 now_ns) const
	{
		if (!ackDirty) return false;

		return pendingAckPacketCount >= AckElicitingPacketThreshold || (now_ns - firstPendingAckTime_ns) >= DelayPiggybackAckTimeout_ns;
	}

	void ReliabilityState::ClearPendingAck()
	{
		ackDirty				= false;
		pendingAckSeq			= 0;
		pendingAckWindow		= 0;
		pendingAckPacketCount	= 0;
		firstPendingAckTime_ns	= 0;
	}

	uint64 ReliabilityState::BuildAckWindow() const
	{
		uint64 bitfield = 0;
		const uint16 base = pendingAckSeq;

		for (uint16 i = 1; i <= AckWindowSize; ++i)
		{
			const uint16 seq = static_cast<uint16>(base - i);
			const uint16 dist = SeqDistance(base, seq);

			if (dist == 0 || dist > AckTrackSize)
				continue;

			if (ackTrack.test(dist))
				bitfield |= (uint64{ 1 } << (i - 1));
		}
		return bitfield;
	}

	// ============================================================
	//  RPC State
	// ============================================================

	uint32 RpcState::GenerateRequestId()
	{
		for (uint32 i = 0; i < std::numeric_limits<uint32>::max(); ++i)
		{
			uint32 id = nextRequestId++;
			if (nextRequestId == 0)
				nextRequestId = 1;

			if (id != 0 && !inflight.contains(id))
				return id;
		}

		return 0;
	}

	bool RpcState::RegisterRequest(uint32 reqId, AwaitState&& state)
	{
		if (reqId == 0)
			return false;

		return inflight.emplace(reqId, std::move(state)).second;
	}

	std::optional<RpcState::AwaitState> RpcState::PopRequest(uint32 reqId)
	{
		const auto it = inflight.find(reqId);
		if (it == inflight.end())
			return std::nullopt;

		auto st = std::move(it->second);
		inflight.erase(it);
		return st;
	}

	std::vector<uint32> RpcState::GetTimedOutRequests(uint64 now_ns) const
	{
		std::vector<uint32> out;
		for (const auto& [id, st] : inflight)
		{
			if (st.hasDeadline && now_ns >= st.deadline_ns)
				out.push_back(id);
		}
		return out;
	}

	// ============================================================
	//  Time Sync
	// ============================================================

	void TimeSyncState::ProcessPingPong(uint64 t1, uint64 t2, uint64 t3, uint64 t4)
	{
		const uint64 rtt = (t4 - t1) - (t3 - t2);
		minRtt_ns		= std::min(minRtt_ns, rtt);
		offset_ns		= (static_cast<int64>(t2 - t1) + static_cast<int64>(t3 - t4)) / 2;
		lastPingSend_ns = t1;
		lastPongRecv_ns = t4;

		rwnd[winHead] = rtt;
		ownd[winHead] = offset_ns;
		winHead = (winHead + 1) % Window;
		if (winCount < Window)
			winCount++;

		if (!bIsStabilized && winCount >= StabilizationThreshold)
		{
			bIsStabilized = true;
			currentPingInterval_ns = PingIntervalStable_ns;
		}
	}

	int64 TimeSyncState::GetServerTime(uint64 clientTime_ns) const
	{
		return static_cast<int64>(clientTime_ns) + offset_ns;
	}

	bool TimeSyncState::ShouldSendPing(uint64 now_ns) const
	{
		return (now_ns - lastPingSend_ns) >= currentPingInterval_ns;
	}

	// ============================================================
	//  Congestion Control
	// ============================================================

	void CongestionState::OnSend(uint32 packetSize)
	{
		bytesInFlight += packetSize;
	}

	void CongestionState::OnAck(uint32 ackedBytes)
	{
		if (bytesInFlight >= ackedBytes)
			bytesInFlight -= ackedBytes;
		else
			bytesInFlight = 0;

		switch (state)
		{
		case SLOW_START:
			cwnd = std::min(maxCwnd, static_cast<uint32>(cwnd + ackedBytes));
			if (cwnd >= ssthresh)
				state = CONGESTION_AVOIDANCE;
			break;
		case CONGESTION_AVOIDANCE:
			cwnd = std::min(maxCwnd, static_cast<uint32>(cwnd + std::max<uint32>(1, (JAMNET_MTU * ackedBytes) / std::max<uint32>(1, cwnd))));
			break;
		case FAST_RECOVERY:
			state = CONGESTION_AVOIDANCE;
			break;
		}
	}

	void CongestionState::OnLoss()
	{
		ssthresh		= std::max(minCwnd, cwnd / 2);
		cwnd			= minCwnd;
		bytesInFlight	= 0;
		duplicateAcks	= 0;
		state			= SLOW_START;
	}

	void CongestionState::OnTimeout()
	{
		OnLoss();
	}

	bool CongestionState::CanSend(uint32 packetSize) const
	{
		return bytesInFlight + packetSize <= cwnd;
	}

	// ============================================================
	//  TransmissionWaitingQueue
	// ============================================================

	void TransmissionWaitingQueue::Enqueue(Packet packet, Priority priority)
	{
		if (!packet.IsValid()) return;

		const uint32 size = packet->Size();
		queue.push_back({ priority, size, packet, {} });
		bytesQueued += size;
	}

	bool TransmissionWaitingQueue::ShouldFlush(uint64 now_ns) const
	{
		if (queue.empty())
			return false;
		if (flushRequested)
			return true;
		if (queue.size() >= MaxTransportBatch)
			return true;
		if (now_ns - lastFlushTime_ns >= FlushInterval_ns)
			return true;
		for (const auto& pkt : queue)
		{
			if (pkt.priority <= ACK_ONLY)
				return true;
		}
		return false;
	}

	void TransmissionWaitingQueue::Clear()
	{
		queue.clear();
		bytesQueued		= 0;
		flushRequested	= false;
	}

} // namespace jam::net
