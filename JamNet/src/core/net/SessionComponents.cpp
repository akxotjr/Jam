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
	void RttState::OnSample(uint64 sample_ns, uint64 peerDelay_ns)
	{
		if (sample_ns == 0)
			return;

		latestRtt_ns = sample_ns;
		minRtt_ns = std::min(minRtt_ns, sample_ns);

		uint64 adjustedSample_ns = sample_ns;
		if (peerDelay_ns != 0 && minRtt_ns != UINT64_MAX && sample_ns > minRtt_ns && sample_ns - minRtt_ns > peerDelay_ns)
			adjustedSample_ns -= peerDelay_ns;

		if (smoothedRtt_ns == 0)
		{
			smoothedRtt_ns = adjustedSample_ns;
			rttVariance_ns = adjustedSample_ns / 2;
			return;
		}

		const uint64 deviation = smoothedRtt_ns > adjustedSample_ns ? smoothedRtt_ns - adjustedSample_ns : adjustedSample_ns - smoothedRtt_ns;

		rttVariance_ns = (3 * rttVariance_ns + deviation) / 4;
		smoothedRtt_ns = (7 * smoothedRtt_ns + adjustedSample_ns) / 8;
	}

	void RttState::OnReliableSample(const uint64 sample_ns, const uint64 peerDelay_ns)
	{
		OnSample(sample_ns, peerDelay_ns);
		if (sample_ns != 0)
			hasReliableSample = true;
	}

	void RttState::OnTimeSyncSample(const uint64 sample_ns)
	{
		if (!hasReliableSample)
			OnSample(sample_ns);
	}

	uint64 RttState::GetRetransmitTimeout(uint8 retryCount) const
	{
		constexpr uint64 InitialRetransmitTimeout_ns = 250_ms;
		constexpr uint64 MinRetransmitTimeout_ns = 50_ms;
		constexpr uint64 MaxRetransmitTimeout_ns = 1_s;
		constexpr uint64 MaxBackoffTimeout_ns = 2_s;
		constexpr uint8 MaxBackoffShift = 4;

		const uint64 baseRto = smoothedRtt_ns == 0 ? InitialRetransmitTimeout_ns : std::clamp(smoothedRtt_ns + 4 * rttVariance_ns, MinRetransmitTimeout_ns, MaxRetransmitTimeout_ns);
		const uint8 shift = std::min(retryCount, MaxBackoffShift);
		return std::min(baseRto << shift, MaxBackoffTimeout_ns);
	}

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

		return true;
	}

	void ReliabilityState::MarkInitialSend(uint16 seq, uint64 now_ns)
	{
		auto* pending = TryGetPending(seq);
		if (!pending || pending->hasInitialSend || !pending->packet.IsValid())
			return;

		pending->hasInitialSend = true;
		pending->sendTime_ns = now_ns;
		pending->lastTransmitTime_ns = now_ns;
		inflightSize += pending->packet->Size();
	}

	std::vector<uint16> ReliabilityState::GetRetransmitNeeded(uint64 now_ns, const RttState& rtt) const
	{
		std::vector<uint16> out;
		for (const auto& [seq, pkt] : reliablePendings)
		{
			if (!pkt.hasInitialSend || pkt.retransmitQueued)
				continue;
			// The transport submit timestamp can be newer than the network tick snapshot.
			if (now_ns < pkt.lastTransmitTime_ns)
				continue;

			if (pkt.fastRetransmitRequested || now_ns - pkt.lastTransmitTime_ns >= rtt.GetRetransmitTimeout(pkt.retryCount))
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

		if (it->second.hasInitialSend && it->second.packet.IsValid())
		{
			const uint32 size = it->second.packet->Size();
			inflightSize = (inflightSize >= size) ? (inflightSize - size) : 0;
		}

		reliablePendings.erase(it);
	}

	void ReliabilityState::MarkReceived(uint16 seq, uint64 now_ns)
	{
		if (!hasReliabilityRecv)
		{
			hasReliabilityRecv = true;
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

		auto wasReceived = [&](uint16 candidate)
		{
			if (SeqGreater(candidate, latestReliabilityRecvSeq))
				return false;

			const uint16 distance = SeqDistance(latestReliabilityRecvSeq, candidate);
			return distance < AckTrackSize && ackTrack.test(distance);
		};

		if (!hasCumulativeAck)
		{
			if (wasReceived(0))
			{
				hasCumulativeAck = true;
				cumulativeRecvSeq = 0;
			}
		}

		if (hasCumulativeAck)
		{
			for (;;)
			{
				const uint16 next = static_cast<uint16>(cumulativeRecvSeq + 1);
				if (!wasReceived(next))
					break;
				cumulativeRecvSeq = next;
			}
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

		pendingAckData = {};
		auto& header = pendingAckData.header;
		header.largestAck = latestReliabilityRecvSeq;
		if (hasCumulativeAck)
		{
			header.flags |= AckFlags::HasCumulativeAck;
			header.cumulativeAck = cumulativeRecvSeq;
		}

		const uint16 distanceLimit = hasCumulativeAck ? std::min<uint16>(SeqDistance(latestReliabilityRecvSeq, cumulativeRecvSeq), AckTrackSize) : AckTrackSize;

		uint16 distance = 0;
		while (distance < distanceLimit && header.rangesCount < kMaxAckRanges)
		{
			uint16 gap = 0;
			while (distance < distanceLimit && !ackTrack.test(distance))
			{
				++gap;
				++distance;
			}
			if (distance == distanceLimit)
				break;

			uint16 length = 0;
			while (distance < distanceLimit && ackTrack.test(distance))
			{
				++length;
				++distance;
			}

			pendingAckData.ranges[header.rangesCount++] = AckRange{ gap, length };
		}
	}

	void ReliabilityState::ProcessAck(const AckData& ack, uint64 ackRecvTime_ns, RttState& rtt)
	{
		auto isAcked = [&](uint16 seq)
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
		};

		std::vector<uint16> ackedSequences;
		ackedSequences.reserve(reliablePendings.size());

		for (const auto& seq : reliablePendings | std::views::keys)
		{
			if (isAcked(seq))
				ackedSequences.push_back(seq);
		}

		for (auto& [pendingSeq, pending] : reliablePendings)
		{
			if (pending.fastRetransmitUsed || isAcked(pendingSeq))
				continue;

			uint8 newerAckCount = 0;
			if (HasFlag(ack.header.flags, AckFlags::HasCumulativeAck) && SeqGreater(ack.header.cumulativeAck, pendingSeq))
				newerAckCount = static_cast<uint8>(std::min<uint16>(SeqDistance(ack.header.cumulativeAck, pendingSeq), FastRetransmitThreshold));

			uint16 cursor = ack.header.largestAck;
			for (uint8 i = 0; i < ack.header.rangesCount && newerAckCount < FastRetransmitThreshold; ++i)
			{
				const AckRange& range = ack.ranges[i];
				cursor = static_cast<uint16>(cursor - range.gap);
				for (uint16 offset = 0; offset < range.length && newerAckCount < FastRetransmitThreshold; ++offset)
				{
					const uint16 ackedSeq = static_cast<uint16>(cursor - offset);
					if (SeqGreater(ackedSeq, pendingSeq))
						++newerAckCount;
				}
				cursor = static_cast<uint16>(cursor - range.length);
			}

			if (newerAckCount >= FastRetransmitThreshold)
			{
				pending.fastRetransmitRequested = true;
				pending.fastRetransmitUsed = true;
			}
		}

		bool sampledRtt = false;
		auto ackOne = [&](uint16 seq)
			{
				auto it = reliablePendings.find(seq);
				if (it == reliablePendings.end())
					return;

				const auto& pending = it->second;
				if (!sampledRtt && pending.hasInitialSend && !pending.hasRetransmitted && ackRecvTime_ns >= pending.sendTime_ns)
				{
					const uint64 sample = ackRecvTime_ns - pending.sendTime_ns;
					const uint64 ackDelay_ns = static_cast<uint64>(ack.header.ackDelay_us) * 1'000ull;
					rtt.OnReliableSample(sample, ackDelay_ns);
					sampledRtt = true;
				}

				if (it->second.hasInitialSend && it->second.packet.IsValid())
				{
					const uint32 size = it->second.packet->Size();
					inflightSize = (inflightSize >= size) ? (inflightSize - size) : 0;
				}

				reliablePendings.erase(it);
			};

		for (uint16 seq : ackedSequences)
			ackOne(seq);

		if (SeqGreater(ack.header.largestAck, lastAckedReliabilitySeq))
			lastAckedReliabilitySeq = ack.header.largestAck;
	}

	bool ReliabilityState::ShouldSendAck(uint64 now_ns) const
	{
		if (!ackDirty) return false;

		return pendingAckPacketCount >= AckElicitingPacketThreshold || (now_ns - firstPendingAckTime_ns) >= DelayedAckTimeout_ns;
	}

	void ReliabilityState::ClearPendingAck()
	{
		ackDirty				= false;
		pendingAckData			= {};
		pendingAckPacketCount	= 0;
		firstPendingAckTime_ns	= 0;
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

	void TimeSyncState::ProcessPingPong(uint64 t1, uint64 t2, uint64 t3, uint64 t4, RttState& rttState)
	{
		if (t4 < t1 || t3 < t2)
			return;

		const uint64 roundTrip_ns = t4 - t1;
		const uint64 peerProcessing_ns = t3 - t2;
		if (roundTrip_ns <= peerProcessing_ns)
			return;

		const uint64 rtt = roundTrip_ns - peerProcessing_ns;
		rttState.OnTimeSyncSample(rtt);
		offset_ns = (static_cast<int64>(t2 - t1) + static_cast<int64>(t3 - t4)) / 2;

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
	//  TransmissionWaitingQueue
	// ============================================================

	void TransmissionWaitingQueue::Enqueue(Packet packet)
	{
		if (!packet.IsValid()) return;

		const uint32 size = packet->Size();
		queue.push_back({ size, packet });
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
		return false;
	}

	void TransmissionWaitingQueue::Clear()
	{
		queue.clear();
		bytesQueued		= 0;
		flushRequested	= false;
	}

} // namespace jam::net
