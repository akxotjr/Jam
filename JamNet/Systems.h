#pragma once
#include "Components.h"
#include "EcsEvents.h"
#include "EcsHandle.h"
#include "ReliableTransportManager.h"


namespace jam::net::ecs
{

#pragma region Reliability System

	static inline bool SeqGreater(uint16 a, uint16 b) { return static_cast<int16>(a - b) > 0; }

	static inline uint32 BuildAckBitfield(const ReliabilityState& r, uint16 latestSeq)
	{
		uint32 bitfield = 0;
		for (uint16 i = 1; i <= BITFIELD_SIZE; ++i) {
			const uint16 seq = static_cast<uint16>(latestSeq - i);
			if (!SeqGreater(latestSeq, seq)) continue;
			if (r.receiveHistory.test(seq % WINDOW_SIZE))
				bitfield |= (1u << (i - 1));
		}
		return bitfield;
	}

	static inline uint32 BuildNackBitfield(const ReliabilityState& r, uint16 missingSeq, uint16 latestSeq)
	{
		uint32 bitfield = 0;
		for (uint16 i = 1; i <= BITFIELD_SIZE; ++i) 
		{
			const uint16 seq = static_cast<uint16>(missingSeq + i);
			if (SeqGreater(seq, latestSeq)) break;
			if (!r.receiveHistory.test(seq % WINDOW_SIZE))
				bitfield |= (1u << (i - 1));
		}
		return bitfield;
	}


	struct ReliabilityHandlers
	{
		entt::registry* R{};

		void OnRecvAck(const EvRecvAck& ev)
		{
			auto& Rg = *R;
			auto& pools = Rg.ctx().get<EcsHandlePools>();
			auto [rs, ep] = Rg.get<ReliabilityState, SessionRef>(ev.e);
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
		void OnRecvData(const EvRecvData& ev)
		{
			auto& Rg = *R;
			auto& pools = Rg.ctx().get<EcsHandlePools>();
			auto [cr, ep] = Rg.get<ReliabilityState, SessionRef>(ev.e);
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
		void OnRecvNack(const EvRecvNack& ev)
		{
			auto& Rg = *R;
			auto& pools = Rg.ctx().get<EcsHandlePools>();
			auto [cr, ep] = Rg.get<ReliabilityState, SessionRef>(ev.e);
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
		void OnDidSend(const EvDidSend& ev)
		{
			auto& Rg = *R;
			auto& pools = Rg.ctx().get<EcsHandlePools>();
			auto& rs = Rg.get<ReliabilityState>(ev.e);
			auto* st = pools.reliability.get(rs.hStore);
			if (!st) return;

			st->pending[ev.seq] = { ev.buf, ev.size, ev.ts, 0 };
			rs.inFlightSize += ev.size;
		}
		void OnPiggybackFailed(const EvPiggybackFailed& ev)
		{
			auto& Rg = *R;
			auto [rs, session] = Rg.get<ReliabilityState, SessionRef>(ev.e);
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


	void ReliabilityWiringSystem(utils::exec::ShardLocal& L, uint64 now_ns, uint64 dt_ns);
	void ReliabilityTickSystem(utils::exec::ShardLocal& L, uint64 now_ns, uint64 dt_ns);

#pragma endregion


#pragma region NetStat System

	void NetStatSystem(utils::exec::ShardLocal& L, uint64 now_ns, uint64 dt_ns);

#pragma endregion

#pragma region CongestionControl System

	void CongestionSystem(utils::exec::ShardLocal& L, uint64 now_ns, uint64 dt_ns);

#pragma endregion

#pragma region Fragment System
	void FragmentSystem(utils::exec::ShardLocal& L, uint64 now_ns, uint64 dt_ns);
#pragma endregion

#pragma region Channel System
	void ChannelSystem(utils::exec::ShardLocal& L, uint64 now_ns, uint64 dt_ns);
#pragma endregion


	void GroupHomeSystem(utils::exec::ShardLocal& L, uint64 now_ns, uint64 dt_ns);

}
