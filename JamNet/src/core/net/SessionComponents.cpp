#include "pch.h"
#include "jamnet/core/net/SessionComponents.h"
#include "jamnet/core/net/Session.h"

namespace jam::net
{
	// ============================================================
	//	SessionInfo
	// ============================================================

	SessionInfo SessionInfo::FromSession(Session* sess, uint64 now_ns)
	{
		SessionInfo comp;
		comp.session			= sess;
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
		if (index >= totalFragments)
			return false;  // invalid fragment

		if (receivedMask.test(index))
			return false;  // already received

		fragments[index].assign(data, data + size);
		receivedMask.set(index);
		receivedCount++;
		lastRecvTime_ns = now_ns;

		return true;
	}

	vector<BYTE> FragmentState::Reassembly::Assemble() const
	{
		vector<BYTE> result;
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

	optional<vector<BYTE>> FragmentState::PopCompleted(const uint16 fragmentId)
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
				it = reassemblies.erase(it);
			}
			else
			{
				++it;
			}
		}
	}



	// ============================================================
	//	NetworkCounter
	// ============================================================

	void NetworkCounter::OnRecv(uint32 bytes)
	{
		totalRecvBytes += bytes;
		totalRecvPackets++;
	}

	void NetworkCounter::OnSend(uint32 bytes)
	{
		totalSendBytes += bytes;
		totalSendPackets++;
	}

	float NetworkCounter::GetRecvThroughput(uint64 interval_ns) const
	{
		if (interval_ns == 0) return 0.0f;
		return (static_cast<float>(totalRecvBytes) * 1e9f) / static_cast<float>(interval_ns);
	}

	float NetworkCounter::GetSendThroughput(uint64 interval_ns) const
	{
		if (interval_ns == 0) return 0.0f;
		return (static_cast<float>(totalSendBytes) * 1e9f) / static_cast<float>(interval_ns);
	}

	

	// ============================================================
	//  NetStat
	// ============================================================

	void CompNetworkStats::AddRttSample(float newRtt_ms)
	{
		// 샘플 저장
		rttSamples[rttSampleIndex] = newRtt_ms;
		rttSampleIndex = (rttSampleIndex + 1) % 32;
		if (rttSampleCount < 32)
			rttSampleCount++;

		// 현재 RTT
		rtt_ms = newRtt_ms;

		// Min/Max
		rttMin_ms = std::min(rttMin_ms, newRtt_ms);
		rttMax_ms = std::max(rttMax_ms, newRtt_ms);

		// 평균 (최근 샘플들)
		float sum = 0.0f;
		for (uint8 i = 0; i < rttSampleCount; ++i)
		{
			sum += rttSamples[i];
		}
		rttAvg_ms = sum / static_cast<float>(rttSampleCount);

		// Jitter (RTT 변동성)
		UpdateJitter();
	}

	void CompNetworkStats::UpdateJitter()
	{
		if (rttSampleCount < 2)
		{
			jitter_ms = 0.0f;
			return;
		}

		float variance = 0.0f;
		for (uint8 i = 0; i < rttSampleCount; ++i)
		{
			float diff = rttSamples[i] - rttAvg_ms;
			variance += diff * diff;
		}
		variance /= static_cast<float>(rttSampleCount);

		jitter_ms = sqrtf(variance);
	}

	void CompNetworkStats::UpdatePacketLoss(const uint32 lost, const uint32 expected)
	{
		lostPackets += lost;
		totalExpected += expected;

		if (totalExpected > 0)
		{
			packetLoss = static_cast<float>(lostPackets) / static_cast<float>(totalExpected);
		}
	}

	void CompNetworkStats::UpdateBandwidth(const uint64 bytes, const uint64 interval_ns)
	{
		if (interval_ns == 0)
			return;

		// bytes/sec -> bits/sec
		estimatedBandwidth_bps = (static_cast<float>(bytes)* 8.0f * 1e9f) / static_cast<float>(interval_ns);
	}

	void CompNetworkStats::OnChannelRecv(eChannelType ch, uint32 bytes)
	{
		channelStats[E2U(ch)].recvBytes += bytes;
		channelStats[E2U(ch)].recvPackets++;
	}

	void CompNetworkStats::OnChannelSend(eChannelType ch, uint32 bytes)
	{
		channelStats[E2U(ch)].sendBytes += bytes;
		channelStats[E2U(ch)].sendPackets++;
	}


	// ============================================================
	//  OrderState
	// ============================================================

	bool OrderState::StoreRecvPacket(uint16 seq, const shared_ptr<RecvBuffer>& buf, uint64 now_ns)
	{
		if (pendings.size() >= kMaxRecvBufferSize)
			return false;

		pendings[seq] = RecvPacket{ seq, now_ns, buf };
		return true;
	}

	vector<OrderState::RecvPacket> OrderState::PopOrderedPackets(uint16& expectedSeq)
	{
		vector<RecvPacket> out;
		while (true)
		{
			auto it = pendings.find(expectedSeq);
			if (it == pendings.end())
				break;

			out.push_back(it->second);
			pendings.erase(it);
			expectedSeq = static_cast<uint16>(expectedSeq + 1);
		}
		return out;
	}


	// ============================================================
	//  ReliabilityState
	// ============================================================

	bool ReliabilityState::StoreSendPacket(eChannelType ch, const shared_ptr<SendBuffer>& buf, uint16 seq, uint64 now_ns)
	{
		auto& chData = GetChannelData(ch);
		if (chData.pendings.contains(seq))
			return false;

		chData.pendings[seq] = PendingPacket{ seq, now_ns, now_ns, 0 , buf };
		if (buf)
			chData.inflightSize += buf->WriteSize();
		return true;
	}

	vector<uint16> ReliabilityState::GetRetransmitNeeded(eChannelType ch, uint64 now_ns) const
	{
		vector<uint16> out;
		const auto& chData = GetChannel(ch);
		for (const auto& [seq, pkt] : chData.pendings)
		{
			if (pkt.retryCount >= MAX_RETRY)
				continue;
			if (now_ns - pkt.lastRetransmitTime_ns >= RETRANSMIT_TIMEOUT_NS)
				out.push_back(seq);
		}
		return out;
	}

	void ReliabilityState::ProcessAck(const eChannelType ch, const uint16 ackSeq, const uint32 ackBitfield)
	{
		auto& chData = GetChannelData(ch);
		auto ackOne = [&](uint16 seq)
		{
			const auto it = chData.pendings.find(seq);
			if (it == chData.pendings.end())
				return;
			if (it->second.buf)
				chData.inflightSize -= it->second.buf->WriteSize();
			chData.pendings.erase(it);
		};

		ackOne(ackSeq);
		for (uint16 i = 1; i <= ACK_WINDOW_SIZE; ++i)
		{
			if (ackBitfield & (1u << (i - 1)))
			{
				uint16 seq = static_cast<uint16>(ackSeq - i);
				ackOne(seq);
			}
		}

		if (SeqGreater(ackSeq, chData.lastAckedSeq))
			chData.lastAckedSeq = ackSeq;
	}

	bool ReliabilityState::ShouldSendAck(eChannelType ch, uint64 now_ns) const
	{
		const auto& chData = GetChannel(ch);
		if (!chData.hasPendingAck)
			return false;
		return (now_ns - chData.firstPendingAckTime_ns) >= DELAY_PIGGYBACK_ACK_TIMEOUT_NS;
	}

	uint32 ReliabilityState::BuildAckWindow(eChannelType ch) const
	{
		const auto& chData = GetChannel(ch);
		uint32 bitfield = 0;

		const uint16 base = chData.pendingAckSeq;

		for (uint16 i = 1; i <= ACK_WINDOW_SIZE; ++i)
		{
			// base보다 i만큼 과거
			const uint16 seq = static_cast<uint16>(base - i);

			// base - seq == i (mod 2^16)
			const uint16 dist = SeqDistance(base, seq);

			// ackTrack은 base 기준 과거 ACK_TRACK_SIZE개만 유효
			if (dist == 0 || dist > ACK_TRACK_SIZE)
				continue;

			const uint32 idx = static_cast<uint32>(dist - 1);
			if (chData.ackTrack.test(idx))
				bitfield |= (1u << (i - 1));
		}

		return bitfield;
	}

	uint32 ReliabilityState::BuildNackWindow(eChannelType ch, uint16 expectedSeq) const
	{
		const auto& chData = GetChannel(ch);
		uint32 bitfield = 0;

		// NACK 윈도우는 expectedSeq 이후(미수신 추정) 구간을 훑는 로직인데,
		// ackTrack의 기준(base=pendingAckSeq)에 대해 "해당 seq가 관측된 적 있는가"로 판정하려면
		// seq가 base 기준 과거에 있어야만 의미가 있음.
		const uint16 base = chData.pendingAckSeq;

		for (uint16 i = 1; i <= ACK_WINDOW_SIZE; ++i)
		{
			const uint16 seq = static_cast<uint16>(expectedSeq + i);

			// 최신 ACK보다 미래면 중단
			if (SeqGreater(seq, chData.latestAckSeq))
				break;

			// base 기준으로 seq가 과거로 ACK_TRACK_SIZE 안에 들어오는지 확인
			const uint16 dist = SeqDistance(base, seq);
			if (dist == 0 || dist > ACK_TRACK_SIZE)
			{
				// ackTrack으로 판정 불가능한 영역(너무 옛날/동일) -> 여기서는 NACK 대상으로 취급하지 않음
				continue;
			}

			const uint32 idx = static_cast<uint32>(dist - 1);
			if (!chData.ackTrack.test(idx))
				bitfield |= (1u << (i - 1));
		}

		return bitfield;
	}

	// ============================================================
	//  RPC State
	// ============================================================

	void RpcState::RegisterRequest(uint32 reqId, AwaitState&& state)
	{
		inflight.emplace(reqId, std::move(state));
	}

	optional<RpcState::AwaitState> RpcState::PopRequest(uint32 reqId)
	{
		const auto it = inflight.find(reqId);
		if (it == inflight.end())
			return std::nullopt;

		auto st = std::move(it->second);
		inflight.erase(it);
		return st;
	}

	vector<uint32> RpcState::GetTimedOutRequests(uint64 now_ns) const
	{
		vector<uint32> out;
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
		minRtt_ns = std::min(minRtt_ns, rtt);
		offset_ns = static_cast<int64>((static_cast<int64>(t2 - t1) + static_cast<int64>(t3 - t4)) / 2);
		lastPingSend_ns = t1;
		lastPongRecv_ns = t4;

		rwnd[winHead] = rtt;
		ownd[winHead] = offset_ns;
		winHead = (winHead + 1) % WIN;
		if (winCount < WIN)
			winCount++;

		if (rtt <= kPingIntervalStable_ns)
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
		ssthresh = std::max(minCwnd, cwnd / 2);
		cwnd = minCwnd;
		bytesInFlight = 0;
		duplicateAcks = 0;
		state = SLOW_START;
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

	void TransmissionWaitingQueue::Enqueue(const shared_ptr<SendBuffer>& buf, Priority prio)
	{
		if (!buf)
			return;
		const uint32 size = buf->WriteSize();
		queue.push_back({ prio, size, buf });
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
		bytesQueued = 0;
		flushRequested = false;
	}



	// ============================================================
	//  NetworkStatsView
	// ============================================================

	NetworkStatsView NetworkStatsView::FromEntity(entt::registry& R, entt::entity e)
	{
		NetworkStatsView view{};
		if (auto* stats = R.try_get<CompNetworkStats>(e))
		{
			view.rtt_ms = stats->rtt_ms;
			view.jitter_ms = stats->jitter_ms;
			view.packetLoss = stats->packetLoss;
		}
		if (auto* counter = R.try_get<NetworkCounter>(e))
		{
			constexpr uint64 interval = 1'000'000'000ull;
			view.recvThroughput_kbps = counter->GetRecvThroughput(interval) / 1000.0f;
			view.sendThroughput_kbps = counter->GetSendThroughput(interval) / 1000.0f;
		}
		return view;
	}

}