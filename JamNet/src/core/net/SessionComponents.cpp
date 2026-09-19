#include "pch.h"
#include "jamnet/core/net/SessionComponents.h"
#include "jamnet/core/net/Session.h"

namespace jam::net
{
	// ============================================================
	//	SessionInfo
	// ============================================================

	SessionInfo SessionInfo::FromSession(Session* session, Timestamp now)
	{
		SessionInfo comp;
		comp.session		= session;
		comp.connectedTime	= now;
		comp.lastRecvTime	= now;
		comp.lastSendTime	= now;
		comp.state			= State::CONNECTED;

		return comp;
	}


	// ============================================================
	//	FragmentState
	// ============================================================

	bool FragmentState::Reassembly::AddFragment(uint8 index, const BYTE* data, uint32 size, Timestamp now)
	{
		if (index >= totalFragments)  // invalid fragment
			return false;

		if (receivedMask.test(index)) // already received
			return false;

		fragments[index].assign(data, data + size);
		receivedMask.set(index);
		receivedCount++;
		lastRecvTime = now;

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


	bool FragmentState::AddFragment(uint16 fragmentId, uint8 totalFragments, const uint8 index, const BYTE* data, const uint32 size, Timestamp now)
	{
		auto it = reassemblies.find(fragmentId);
		if (it == reassemblies.end())
		{
			auto [newIt, inserted] = reassemblies.emplace(
				std::piecewise_construct,
				std::forward_as_tuple(fragmentId),
				std::forward_as_tuple(fragmentId, totalFragments, now)
			);
			it = newIt;
		}

		return it->second.AddFragment(index, data, size, now);
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

	void FragmentState::CleanupTimeouts(const Timestamp now)
	{
		for (auto it = reassemblies.begin(); it != reassemblies.end(); )
		{
			if (now - it->second.lastRecvTime > Timeout)
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

	bool OrderState::StoreRecvPacket(uint16 orderSeq, uint16 span, ::jam::net::Packet packet, Timestamp now)
	{
		if (pendings.size() >= MaxRecvBufferSize)
			return false;

		if (pendings.contains(orderSeq))
			return false;

		pendings.emplace(orderSeq, OrderedPacket{
			.orderSeq    = orderSeq,
			.span		 = span,
			.recvTime    = now,
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
	void RttState::OnSample(Duration sample, Duration peerDelay)
	{
		if (sample.IsZero())
			return;

		latestRtt = sample;
		minRtt = std::min(minRtt, sample);

		Duration adjustedSample = sample;
		if (!peerDelay.IsZero() && minRtt != Duration::Max() && sample > minRtt && sample - minRtt > peerDelay)
			adjustedSample -= peerDelay;

		if (smoothedRtt.IsZero())
		{
			smoothedRtt = adjustedSample;
			rttVariance = adjustedSample / 2;
			return;
		}

		const Duration deviation = smoothedRtt > adjustedSample ? smoothedRtt - adjustedSample : adjustedSample - smoothedRtt;

		rttVariance = (3 * rttVariance + deviation) / 4;
		smoothedRtt = (7 * smoothedRtt + adjustedSample) / 8;
	}

	void RttState::OnReliableSample(const Duration sample, const Duration peerDelay)
	{
		OnSample(sample, peerDelay);
		if (!sample.IsZero())
			hasReliableSample = true;
	}

	void RttState::OnTimeSyncSample(const Duration sample)
	{
		if (!hasReliableSample)
			OnSample(sample);
	}

	Duration RttState::GetRetransmitTimeout(uint8 retryCount) const
	{
		constexpr Duration InitialRetransmitTimeout = 250_ms;
		constexpr Duration MinRetransmitTimeout = 50_ms;
		constexpr Duration MaxRetransmitTimeout = 1_s;
		constexpr Duration MaxBackoffTimeout = 2_s;
		constexpr uint8 MaxBackoffShift = 4;

		const Duration baseRto = smoothedRtt.IsZero() ? InitialRetransmitTimeout : std::clamp(smoothedRtt + 4 * rttVariance, MinRetransmitTimeout, MaxRetransmitTimeout);
		const uint8 shift = std::min(retryCount, MaxBackoffShift);
		return std::min(baseRto * (1ull << shift), MaxBackoffTimeout);
	}

	bool ReliabilityState::StoreSendPacket(eChannel ch, Packet packet, uint16 reliabilitySeq, Timestamp now)
	{
		if (!IsReliableChannel(ch))
			return false;

		if (reliablePendings.contains(reliabilitySeq))
			return false;

		reliablePendings.emplace(reliabilitySeq, PendingPacket{
				.reliabilitySeq	   = reliabilitySeq,
				.channel			   = ch,
				.sendTime			   = now,
				.lastTransmitTime	   = now,
				.retryCount			   = 0,
				.packet				   = packet
			});

		return true;
	}

	void ReliabilityState::MarkInitialSend(uint16 seq, Timestamp now)
	{
		auto* pending = TryGetPending(seq);
		if (!pending || pending->hasInitialSend || !pending->packet.IsValid())
			return;

		pending->hasInitialSend = true;
		pending->sendTime = now;
		pending->lastTransmitTime = now;
		inflightSize += pending->packet->Size();
	}

	std::vector<uint16> ReliabilityState::GetRetransmitNeeded(Timestamp now, const RttState& rtt) const
	{
		std::vector<uint16> out;
		for (const auto& [seq, pkt] : reliablePendings)
		{
			if (!pkt.hasInitialSend || pkt.retransmitQueued)
				continue;
			// The transport submit timestamp can be newer than the network tick snapshot.
			if (now < pkt.lastTransmitTime)
				continue;

			if (pkt.fastRetransmitRequested || now - pkt.lastTransmitTime >= rtt.GetRetransmitTimeout(pkt.retryCount))
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

	void ReliabilityState::MarkReceived(uint16 seq, Timestamp now)
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

		MarkAckPending(now);
	}

	void ReliabilityState::MarkAckPending(Timestamp now)
	{
		if (!ackDirty)
		{
			ackDirty = true;
			firstPendingAckTime = now;
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

	void ReliabilityState::ProcessAck(const AckData& ack, Timestamp ackRecvTime, RttState& rtt)
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
				if (!sampledRtt && pending.hasInitialSend && !pending.hasRetransmitted && ackRecvTime >= pending.sendTime)
				{
					const Duration sample = ackRecvTime - pending.sendTime;
					const Duration ackDelay = Duration::FromUs(ack.header.ackDelay_us);
					rtt.OnReliableSample(sample, ackDelay);
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

	bool ReliabilityState::ShouldSendAck(Timestamp now) const
	{
		if (!ackDirty) return false;

		return pendingAckPacketCount >= AckElicitingPacketThreshold || (now - firstPendingAckTime) >= DelayedAckTimeout;
	}

	void ReliabilityState::ClearPendingAck()
	{
		ackDirty				= false;
		pendingAckData			= {};
		pendingAckPacketCount	= 0;
		firstPendingAckTime		= Timestamp::Zero();
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

	std::vector<uint32> RpcState::GetTimedOutRequests(Timestamp now) const
	{
		std::vector<uint32> out;
		for (const auto& [id, st] : inflight)
		{
			if (st.hasDeadline && now >= st.deadline)
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

		const Duration rtt = Duration::FromNs(roundTrip_ns - peerProcessing_ns);
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
			currentPingInterval = PingIntervalStable;
		}
	}

	int64 TimeSyncState::GetServerTime(uint64 clientTime_ns) const
	{
		return static_cast<int64>(clientTime_ns) + offset_ns;
	}

	bool TimeSyncState::ShouldSendPing(Timestamp now) const
	{
		return (now - lastPingSend) >= currentPingInterval;
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

	bool TransmissionWaitingQueue::ShouldFlush(Timestamp now) const
	{
		if (queue.empty())
			return false;
		if (flushRequested)
			return true;
		if (queue.size() >= MaxTransportBatch)
			return true;
		if (now - lastFlushTime >= FlushInterval)
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
