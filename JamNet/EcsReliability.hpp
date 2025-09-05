#pragma once

#include "pch.h"

#include "EcsHandle.h"
#include "ReliableTransportManager.h"

namespace jam::net::ecs
{
	struct CompEndpoint;
	// Components

	struct CompReliability
	{
		// 송신/수신 창
		uint16  sendSeq = 1;
		uint16  latestSeq = 0;
		uint16  expectedNextSeq = 1;
		std::bitset<WINDOW_SIZE> receiveHistory;

		// in-flight(바이트) / 지연 ACK
		uint32  inFlightSize = 0;
		bool    hasPendingAck = false;
		uint16  pendingAckSeq = 0;
		uint32  pendingAckBitfield = 0;
		uint64  firstPendingAckTick = 0;

		// NACK 타이밍
		uint64  lastNackTime = 0;

		// 콜드 스토어 핸들
		EcsHandle hStore = EcsHandle::invalid();
	};

	struct ReliabilityStore
	{
		xumap<uint16, PendingPacketInfo> pending;   // seq -> {buf,size,timestamp,retry}
		xumap<uint16, uint32>            dupAckCount;
		xuset<uint16>                    sentNackSeqs;
		uint16                           lastAckedSeq = 0;
	};

	// Events

	struct EvRRecvAck
	{
		entt::entity   e{ entt::null };
		uint16         latestSeq{};
		uint32         bitfield{};
	};

	struct EvRRecvData
	{
		entt::entity e{ entt::null };
		uint16 seq;
	};

	struct EvRRecvNack
	{
		entt::entity e{ entt::null };
		uint16 missingSeq;
		uint32 bitfield;
	};

	struct EvRDidSend
	{
		entt::entity e{ entt::null };
		Sptr<SendBuffer> buf;
		uint16 seq;
		uint32 size;
		uint64 ts;
	};

	struct EvRPiggybackFailed
	{
		entt::entity e{ entt::null };
	};

	// Handlers

	struct ReliabilityHandlers
	{
		entt::registry* R{};

		void OnRecvAck(const EvRRecvAck& ev)
		{
			auto& Rg = *R;
			auto& pools = Rg.ctx().get<EcsHandlePools>();
			auto [rs, ep] = Rg.get<CompReliability, SessionRef>(ev.e);
			auto* st = pools.reliability.get(rs.hStore);
			if (!st) return;

			// 확인된 펜딩 제거
			for (uint16 i = 0; i <= BITFIELD_SIZE; ++i)
			{
				const uint16 ackSeq = static_cast<uint16>(ev.latestSeq - i);
				const bool acked = (i == 0) || (ev.bitfield & (1u << (i - 1)));
				if (!acked) continue;
				if (auto it = st->pending.find(ackSeq); it != st->pending.end())
				{
					rs.inFlightSize -= it->second.size;
					st->pending.erase(it);
					st->dupAckCount.erase(ackSeq);
				}
			}

			// Fast RTX (중복 ACK)  — 원본 CheckFastRTX와 동등
			if (!SeqGreater(ev.latestSeq, st->lastAckedSeq))
			{
				auto& cnt = st->dupAckCount[ev.latestSeq];
				if (++cnt >= 3)
				{
					const uint16 missingSeq = static_cast<uint16>(ev.latestSeq + 1);
					if (auto it = st->pending.find(missingSeq); it != st->pending.end())
					{
						auto udp = static_pointer_cast<UdpSession>(ep.wp.lock());
						udp->SendDirect(it->second.buffer);
						it->second.retryCount++;
						it->second.timestamp = utils::Clock::Instance().NowNs();
						udp->GetNetStatManager()->OnFastRTX();
						udp->GetCongestionController()->OnFastRTX();
					}
					cnt = 0;
				}
			}
			else
			{
				st->dupAckCount.clear();
				st->lastAckedSeq = ev.latestSeq;
			}
		}
		void OnRecvData(const EvRRecvData& ev)
		{
			auto& Rg = *R;
			auto& pools = Rg.ctx().get<EcsHandlePools>();
			auto [cr, ep] = Rg.get<CompReliability, SessionRef>(ev.e);
			auto* st = pools.reliability.get(cr.hStore);
			if (!st) return;

			const uint16 seq = ev.seq;

			if (!SeqGreater(seq, static_cast<uint16>(cr.latestSeq - WINDOW_SIZE))) return;
			if (cr.receiveHistory.test(seq % WINDOW_SIZE)) return;

			// gap → NACK 고려 (ShouldSendNack와 동등)
			if (SeqGreater(seq, cr.expectedNextSeq))
			{
				const uint64 now = jam::utils::Clock::Instance().NowNs();
				if ((now - cr.lastNackTime) >= jam::net::ReliableTransportManager::NACK_THROTTLE_INTERVAL &&
					!st->sentNackSeqs.contains(cr.expectedNextSeq) &&
					SeqGreater(seq, static_cast<uint16>(cr.expectedNextSeq + 1))) {
					const uint32 nack = BuildNackBitfield(cr, cr.expectedNextSeq, cr.latestSeq);
					ep.owner->SendDirect(PacketBuilder::CreateNackPacket(cr.expectedNextSeq, nack));
					cr.lastNackTime = now;
					st->sentNackSeqs.insert(cr.expectedNextSeq);
				}
			}

			// 수신 처리
			cr.receiveHistory.set(seq % WINDOW_SIZE);
			if (SeqGreater(seq, cr.latestSeq)) cr.latestSeq = seq;

			if (seq == cr.expectedNextSeq)
			{
				++cr.expectedNextSeq;
				while (cr.receiveHistory.test(cr.expectedNextSeq % WINDOW_SIZE))
					++cr.expectedNextSeq;
			}

			// 지연 ACK 예약 (SetPendingAck와 동등)
			const uint64 now = jam::utils::Clock::Instance().GetCurrentTick();
			if (!cr.hasPendingAck)
			{
				cr.hasPendingAck = true;
				cr.pendingAckSeq = seq;
				cr.firstPendingAckTick = now;
			}
			else if (SeqGreater(seq, cr.pendingAckSeq))
			{
				cr.pendingAckSeq = seq;
			}
			cr.pendingAckBitfield = BuildAckBitfield(cr, cr.pendingAckSeq);
		}
		void OnRecvNack(const EvRRecvNack& ev)
		{
			auto& Rg = *R;
			auto& pools = Rg.ctx().get<EcsHandlePools>();
			auto [cr, ep] = Rg.get<CompReliability, SessionRef>(ev.e);
			auto* st = pools.reliability.get(cr.hStore);
			if (!st) return;

			auto trigger = [&](uint16 seq) {
				if (auto it = st->pending.find(seq); it != st->pending.end())
				{
					ep.wp.lock()->SendDirect(it->second.buffer);
					it->second.retryCount++;
					it->second.timestamp = jam::utils::Clock::Instance().NowNs();
					ep.wp.lock()->GetNetStatManager()->OnFastRTX();
					ep.wp.lock()->GetCongestionController()->OnFastRTX();
				}
				};
			trigger(ev.missingSeq);
			for (uint16 i = 1; i <= BITFIELD_SIZE; ++i)
				if (ev.bitfield & (1u << (i - 1)))
					trigger(static_cast<uint16>(ev.missingSeq + i));
		}
		void OnDidSend(const EvRDidSend& ev)
		{
			auto& Rg = *R;
			auto& pools = Rg.ctx().get<EcsHandlePools>();
			auto& rs = Rg.get<CompReliability>(ev.e);
			auto* st = pools.reliability.get(rs.hStore);
			if (!st) return;

			st->pending[ev.seq] = { ev.buf, ev.size, ev.ts, 0 };
			rs.inFlightSize += ev.size;
		}
		void OnPiggybackFailed(const EvRPiggybackFailed& ev)
		{
			auto& Rg = *R;
			auto [rs, session] = Rg.get<CompReliability, SessionRef>(ev.e);
			const uint64 now = jam::utils::Clock::Instance().NowNs();
			if (rs.hasPendingAck && (now - rs.firstPendingAckTick) >= MAX_DELAY_TICK_PIGGYBACK_ACK)
			{
				session.wp.lock()->SendDirect(PacketBuilder::CreateReliabilityAckPacket(rs.pendingAckSeq, rs.pendingAckBitfield));
				rs.hasPendingAck = false;
				rs.pendingAckSeq = 0;
				rs.pendingAckBitfield = 0;
				rs.firstPendingAckTick = 0;
			}
		}
	};

	// Sinks

	struct ReliabilitySinks
	{
		bool wired = false;

		entt::scoped_connection onAck;
		entt::scoped_connection onNack;
		entt::scoped_connection onData;
		entt::scoped_connection onDidSend;
		entt::scoped_connection onPiggyFail;

		ReliabilityHandlers handlers;
	};

	// Systems

	static inline bool SeqGreater(uint16 a, uint16 b) { return static_cast<int16>(a - b) > 0; }

	static inline uint32 BuildAckBitfield(const CompReliability& cr, uint16 latestSeq)
	{
		uint32 bitfield = 0;
		for (uint16 i = 1; i <= BITFIELD_SIZE; ++i) {
			const uint16 seq = static_cast<uint16>(latestSeq - i);
			if (!SeqGreater(latestSeq, seq)) continue;
			if (cr.receiveHistory.test(seq % WINDOW_SIZE))
				bitfield |= (1u << (i - 1));
		}
		return bitfield;
	}

	static inline uint32 BuildNackBitfield(const CompReliability& cr, uint16 missingSeq, uint16 latestSeq)
	{
		uint32 bitfield = 0;
		for (uint16 i = 1; i <= BITFIELD_SIZE; ++i)
		{
			const uint16 seq = static_cast<uint16>(missingSeq + i);
			if (SeqGreater(seq, latestSeq)) break;
			if (!cr.receiveHistory.test(seq % WINDOW_SIZE))
				bitfield |= (1u << (i - 1));
		}
		return bitfield;
	}

	void ReliabilityWiringSystem(utils::exec::ShardLocal& L, uint64, uint64)
	{
		auto& R = L.world;
		auto& D = L.events;

		auto& sinks = R.ctx().emplace<ReliabilitySinks>();

		if (sinks.wired) return;

		sinks.handlers.R = &R; // 핸들러에 레지스트리 주입

		sinks.onAck = D.sink<EvRRecvAck>().connect<&ReliabilityHandlers::OnRecvAck>(&sinks.handlers);
		sinks.onData = D.sink<EvRRecvData>().connect<&ReliabilityHandlers::OnRecvData>(&sinks.handlers);
		sinks.onNack = D.sink<EvRRecvNack>().connect<&ReliabilityHandlers::OnRecvNack>(&sinks.handlers);
		sinks.onDidSend = D.sink<EvRDidSend>().connect<&ReliabilityHandlers::OnDidSend>(&sinks.handlers);
		sinks.onPiggyFail = D.sink<EvRPiggybackFailed>().connect<&ReliabilityHandlers::OnPiggybackFailed>(&sinks.handlers);

		sinks.wired = true;
	}

	void ReliabilityTickSystem(utils::exec::ShardLocal& L, uint64 now_ns, uint64 dt_ns)
	{
		auto& R = L.world;
		auto& pools = R.ctx().get<EcsHandlePools>();
		const uint64 now = jam::utils::Clock::Instance().NowNs();

		auto view = R.view<CompReliability, CompEndpoint>();
		for (auto e : view)
		{
			auto& rs = view.get<CompReliability>(e);
			auto& ep = view.get<CompEndpoint>(e);
			auto* st = pools.reliability.get(rs.hStore);
			if (!st) continue;

			if (st->pending.empty())
			{
				// 지연 ACK 즉시 전송 판단
				if (rs.hasPendingAck && (now - rs.firstPendingAckTick) >= MAX_DELAY_TICK_PIGGYBACK_ACK)
				{
					ep.owner->SendDirect(PacketBuilder::CreateReliabilityAckPacket(rs.pendingAckSeq, rs.pendingAckBitfield));
					rs.hasPendingAck = false; rs.pendingAckSeq = 0; rs.pendingAckBitfield = 0; rs.firstPendingAckTick = 0;
				}
				continue;
			}

			xvector<uint16> toRemove;
			xvector<uint16> toRTX;

			for (auto& [seq, pk] : st->pending)
			{
				const uint64 elapsed = now - pk.timestamp;
				if (elapsed >= RETRANSMIT_TIMEOUT || pk.retryCount >= MAX_RETRY_COUNT)
				{
					toRemove.push_back(seq);
					ep.owner->GetNetStatManager()->OnPacketLoss();
				}
				else if (elapsed >= RETRANSMIT_INTERVAL)
				{
					toRTX.push_back(seq);
				}
			}

			for (auto seq : toRemove)
			{
				if (auto it = st->pending.find(seq); it != st->pending.end())
				{
					rs.inFlightSize -= it->second.size;
					st->pending.erase(it);
				}
			}
			for (auto seq : toRTX) {
				if (auto it = st->pending.find(seq); it != st->pending.end())
				{
					ep.owner->SendDirect(it->second.buffer);
					it->second.retryCount++;
					it->second.timestamp = now;
					ep.owner->GetNetStatManager()->OnRTO();
					ep.owner->GetCongestionController()->OnPacketLoss();
				}
			}
			// 원본: Update()의 RTO/RTX 동작을 그대로 반영. :contentReference[oaicite:7]{index=7}

			// (옵션) 펜딩이 있어도 지연 ACK 임계면 플러시
			if (rs.hasPendingAck && (now - rs.firstPendingAckTick) >= MAX_DELAY_TICK_PIGGYBACK_ACK)
			{
				ep.owner->SendDirect(PacketBuilder::CreateReliabilityAckPacket(rs.pendingAckSeq, rs.pendingAckBitfield));
				rs.hasPendingAck = false; rs.pendingAckSeq = 0; rs.pendingAckBitfield = 0; rs.firstPendingAckTick = 0;
			}
		}
	}

}
