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
		comp.state				= State::CONNECTING;

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
			if (now_ns - it->second.lastRecvTime_ns > REASSEMBLY_TIMEOUT_NS)
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

	bool OrderState::StoreRecvPacket(uint16 orderedSeq, uint16 span, ::jam::net::Packet packet, uint64 now_ns)
	{
		if (pendings.size() >= kMaxRecvBufferSize)
			return false;

		if (pendings.contains(orderedSeq))
			return false;

		pendings.emplace(orderedSeq, OrderedPacket{
			.orderedSeq  = orderedSeq,
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

	bool ReliabilityState::StoreSendPacket(eChannel ch, Packet packet, uint16 seq, uint64 now_ns)
	{
		if (!IsReliableChannel(ch))
			return false;

		if (reliablePendings.contains(seq))
			return false;

		reliablePendings.emplace(seq, PendingPacket{
				.seq				   = seq,
				.channel			   = ch,
				.sendTime_ns		   = now_ns,
				.lastRetransmitTime_ns = now_ns,
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
			if (!pkt.hasInitialSend)
				continue;

			if (pkt.retryCount >= MAX_RETRY)
				continue;

			if (now_ns - pkt.lastRetransmitTime_ns >= RETRANSMIT_TIMEOUT_NS)
				out.push_back(seq);
		}
		return out;
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
		if (ackTrack.none() && latestRecvSeq == 0)
		{
			latestRecvSeq = seq;
			ackTrack.reset();
			ackTrack.set(0);
		}
		else if (SeqGreater(seq, latestRecvSeq))
		{
			const uint16 advance = SeqDistance(seq, latestRecvSeq);

			if (advance >= ACK_TRACK_SIZE)
				ackTrack.reset();
			else
				ackTrack <<= advance;

			latestRecvSeq = seq;
			ackTrack.set(0);
		}
		else
		{
			const uint16 dist = SeqDistance(latestRecvSeq, seq);
			if (dist < ACK_TRACK_SIZE)
				ackTrack.set(dist);
		}

		if (!ackDirty)
		{
			ackDirty = true;
			firstPendingAckTime_ns = now_ns;
		}

		BuildPendingAck();
	}

	void ReliabilityState::BuildPendingAck()
	{
		if (!ackDirty)
			return;

		pendingAckSeq	   = latestRecvSeq;
		pendingAckBitfield = BuildAckWindow();
	}

	void ReliabilityState::ProcessAck(uint16 ackSeq, uint32 ackBitfield)
	{
		auto ackOne = [&](uint16 seq)
			{
				auto it = reliablePendings.find(seq);
				if (it == reliablePendings.end())
					return;

				if (it->second.packet.IsValid())
				{
					const uint32 size = it->second.packet->Size();
					inflightSize = (inflightSize >= size) ? (inflightSize - size) : 0;
				}

				reliablePendings.erase(it);
			};

		ackOne(ackSeq);

		for (uint16 i = 1; i <= ACK_WINDOW_SIZE; ++i)
		{
			if (ackBitfield & (1u << (i - 1)))
				ackOne(static_cast<uint16>(ackSeq - i));
		}

		if (SeqGreater(ackSeq, lastAckedSeq))
			lastAckedSeq = ackSeq;
	}

	bool ReliabilityState::ShouldSendAck(uint64 now_ns) const
	{
		if (!ackDirty) return false;
		return (now_ns - firstPendingAckTime_ns) >= DELAY_PIGGYBACK_ACK_TIMEOUT_NS;
	}

	void ReliabilityState::ClearPendingAck()
	{
		ackDirty				= false;
		pendingAckSeq			= 0;
		pendingAckBitfield		= 0;
		firstPendingAckTime_ns	= 0;
	}

	uint32 ReliabilityState::BuildAckWindow() const
	{
		uint32 bitfield = 0;
		const uint16 base = pendingAckSeq;

		for (uint16 i = 1; i <= ACK_WINDOW_SIZE; ++i)
		{
			const uint16 seq = static_cast<uint16>(base - i);
			const uint16 dist = SeqDistance(base, seq);

			if (dist == 0 || dist > ACK_TRACK_SIZE)
				continue;

			if (ackTrack.test(dist))
				bitfield |= (1u << (i - 1));
		}
		return bitfield;
	}

	uint32 ReliabilityState::BuildNackWindow(uint16 expectedSeq) const
	{
		uint32 bitfield = 0;

		// NACK 윈도우는 expectedSeq 이후(미수신 추정) 구간을 훑는 로직인데,
		// ackTrack의 기준(base=pendingAckSeq)에 대해 "해당 seq가 관측된 적 있는가"로 판정하려면
		// seq가 base 기준 과거에 있어야만 의미가 있음.
		const uint16 base = pendingAckSeq;

		for (uint16 i = 1; i <= ACK_WINDOW_SIZE; ++i)
		{
			const uint16 seq = static_cast<uint16>(expectedSeq + i);

			// 최신 ACK보다 미래면 중단
			if (SeqGreater(seq, latestRecvSeq))
				break;

			// base 기준으로 seq가 과거로 ACK_TRACK_SIZE 안에 들어오는지 확인
			const uint16 dist = SeqDistance(base, seq);
			if (dist == 0 || dist > ACK_TRACK_SIZE)
			{
				// ackTrack으로 판정 불가능한 영역(너무 옛날/동일) -> 여기서는 NACK 대상으로 취급하지 않음
				continue;
			}

			if (!ackTrack.test(dist))
				bitfield |= (1u << (i - 1));
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
		winHead = (winHead + 1) % WIN;
		if (winCount < WIN)
			winCount++;

		if (!bIsStabilized && winCount >= kStabilizationThreshold)
		{
			bIsStabilized = true;
			currentPingInterval_ns = kPingIntervalStable_ns;
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
		if (queue.size() >= kMaxTransportBatch)
			return true;
		if (now_ns - lastFlushTime_ns >= kTransportFlushInterval_ns)
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
