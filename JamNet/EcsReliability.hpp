#pragma once

#include "pch.h"

#include "EcsCommon.hpp"
#include "EcsHandle.h"
#include "EcsTransportAPI.hpp"
#include "PacketBuilder.h"

#include "ReliableTransportManager.h"	//temp



namespace jam::net::ecs
{
	struct EvNsOnFastRTX;
	struct EvCCFastRTX;

	// Components

	struct CompReliability
	{
		// 송신/수신 창
		uint16                      sendSeq = 1;
		uint16                      latestSeq = 0;
		uint16                      expectedNextSeq = 1;
		std::bitset<WINDOW_SIZE>    receiveHistory;

		// in-flight(바이트) / 지연 ACK
		uint32                      inFlightSize = 0;
		bool                        hasPendingAck = false;
		uint16                      pendingAckSeq = 0;
		uint32                      pendingAckBitfield = 0;
		uint64                      firstPendingAckTime_ns = 0_ns;

		// NACK 타이밍
		uint64                      lastNackTime_ns = 0_ns;

		// 콜드 스토어 핸들
		EcsHandle                   hStore = EcsHandle::invalid();
	};

	struct ReliabilityStore
	{
		xumap<uint16, PendingPacketInfo> pending;   // seq -> {buf,size,timestamp,retry}
		xumap<uint16, uint32>            dupAckCount;
		xuset<uint16>                    sentNackSeqs;
		uint16                           lastAckedSeq = 0;
	};

	// Events

	struct EvReSendR
	{
		entt::entity        e{ entt::null };
		Sptr<SendBuffer>    buf;
		uint16              seq;
		uint32              size;
		uint64              ts;
	};

	struct EvReRecvR
	{
		entt::entity        e{ entt::null };
		uint16              seq;
	};

	struct EvReRecvACK
	{
		entt::entity        e{ entt::null };
		uint16              latestSeq{};
		uint32              bitfield{};
	};

	struct EvReRecvNACK
	{
		entt::entity        e{ entt::null };
		uint16              missingSeq;
		uint32              bitfield;
	};


	// Utils

	static inline uint16 AllocSeqRange(entt::registry& R, entt::entity e, uint8 count)
	{
		auto& cr = R.get<CompReliability>(e);
		uint16 base = cr.sendSeq;
		cr.sendSeq = static_cast<uint16>(cr.sendSeq + count); // 16-bit wrap 자연스럽게 처리
		return base;
	}

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

	// Handlers

	struct ReliabilityHandlers
	{
		entt::registry* R{};

		// 신뢰 패킷 송신 등록 (실제 전송은 Transport Flush 단계)
		void OnSendR(const EvReSendR& ev)
		{
			auto& pools = R->ctx().get<EcsHandlePools>();
			auto& cr = R->get<CompReliability>(ev.e);
			auto* st = pools.reliability.get(cr.hStore);
			if (!st) return;

			st->pending[ev.seq] = { ev.buf, ev.size, ev.ts, 0 };
			cr.inFlightSize += ev.size;

			//ecs::EnqueueSend(/**R,*/ ev.e, ev.buf, eTxReason::NORMAL);
		}

		void OnRecvR(const EvReRecvR& ev)
		{
			auto& pools = R->ctx().get<EcsHandlePools>();
			auto [cr, ep] = R->get<CompReliability, CompEndpoint>(ev.e);
			auto* st = pools.reliability.get(cr.hStore);
			if (!st) return;

			const uint16 seq = ev.seq;

			if (!SeqGreater(seq, static_cast<uint16>(cr.latestSeq - WINDOW_SIZE))) return;
			if (cr.receiveHistory.test(seq % WINDOW_SIZE)) return;

			// gap -> NACK 고려
			if (SeqGreater(seq, cr.expectedNextSeq))
			{
				const uint64 now = utils::Clock::Instance().NowNs();
				if ((now - cr.lastNackTime_ns) >= ReliableTransportManager::NACK_THROTTLE_INTERVAL &&
					!st->sentNackSeqs.contains(cr.expectedNextSeq) &&
					SeqGreater(seq, static_cast<uint16>(cr.expectedNextSeq + 1)))
				{
					const uint32 nack = BuildNackBitfield(cr, cr.expectedNextSeq, cr.latestSeq);
					ecs::EnqueueSend(/**R, */ev.e, PacketBuilder::CreateNackPacket(cr.expectedNextSeq, nack), eTxReason::CONTROL);
					cr.lastNackTime_ns = now;
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

			// 지연 ACK 예약
			const uint64 now_ns = utils::Clock::Instance().NowNs();
			if (!cr.hasPendingAck)
			{
				cr.hasPendingAck = true;
				cr.pendingAckSeq = seq;
				cr.firstPendingAckTime_ns = now_ns;
			}
			else if (SeqGreater(seq, cr.pendingAckSeq))
			{
				cr.pendingAckSeq = seq;
			}
			cr.pendingAckBitfield = BuildAckBitfield(cr, cr.pendingAckSeq);
		}

		void OnRecvACK(const EvReRecvACK& ev)
		{
			auto& pools = R->ctx().get<EcsHandlePools>();
			auto [cr, ep] = R->get<CompReliability, CompEndpoint>(ev.e);
			auto* st = pools.reliability.get(cr.hStore);
			if (!st) return;

			// 확인된 펜ding 제거
			for (uint16 i = 0; i <= BITFIELD_SIZE; ++i)
			{
				const uint16 ackSeq = static_cast<uint16>(ev.latestSeq - i);
				const bool acked = (i == 0) || (ev.bitfield & (1u << (i - 1)));
				if (!acked) continue;
				if (auto it = st->pending.find(ackSeq); it != st->pending.end())
				{
					cr.inFlightSize -= it->second.size;
					st->pending.erase(it);
					st->dupAckCount.erase(ackSeq);
				}
			}

			// Fast RTX (중복 ACK → 누락 추정)
			if (!SeqGreater(ev.latestSeq, st->lastAckedSeq))
			{
				auto& cnt = st->dupAckCount[ev.latestSeq];
				if (++cnt >= 3)
				{
					const uint16 missingSeq = static_cast<uint16>(ev.latestSeq + 1);
					if (auto it = st->pending.find(missingSeq); it != st->pending.end())
					{
						ecs::EnqueueSend(*R, ev.e, it->second.buffer, eTxReason::RETRANSMIT);
						it->second.retryCount++;
						it->second.timestamp = utils::Clock::Instance().NowNs();

						R->ctx().get<entt::dispatcher>().enqueue<EvNsOnFastRTX>(EvNsOnFastRTX{ ev.e });
						R->ctx().get<entt::dispatcher>().enqueue<EvCCFastRTX>(EvCCFastRTX{ ev.e });
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

		void OnRecvNACK(const EvReRecvNACK& ev)
		{
			auto& pools = R->ctx().get<EcsHandlePools>();
			auto [cr, ep] = R->get<CompReliability, CompEndpoint>(ev.e);
			auto* st = pools.reliability.get(cr.hStore);
			if (!st) return;

			auto trigger = [&](uint16 seq) {
				if (auto it = st->pending.find(seq); it != st->pending.end())
				{
					ecs::EnqueueSend(*R, ev.e, it->second.buffer, eTxReason::RETRANSMIT);
					it->second.retryCount++;
					it->second.timestamp = utils::Clock::Instance().NowNs();

					R->ctx().get<entt::dispatcher>().enqueue<EvNsOnFastRTX>(EvNsOnFastRTX{ ev.e });
					R->ctx().get<entt::dispatcher>().enqueue<EvCCFastRTX>(EvCCFastRTX{ ev.e });
				}
			};
			trigger(ev.missingSeq);
			for (uint16 i = 1; i <= BITFIELD_SIZE; ++i)
				if (ev.bitfield & (1u << (i - 1)))
					trigger(static_cast<uint16>(ev.missingSeq + i));
		}
	};

	// Sinks

	struct ReliabilitySinks
	{
		bool                    wired = false;

		entt::scoped_connection onSendR;
		entt::scoped_connection onRecvR;
		entt::scoped_connection onRecvACK;
		entt::scoped_connection onRecvNACK;

		ReliabilityHandlers     handlers;
	};

	// Systems

	inline void ReliabilityWiringSystem(utils::exec::ShardLocal& L, uint64, uint64)
	{
		auto& R = L.world;
		auto& D = L.events;

		auto& sinks = R.ctx().emplace<ReliabilitySinks>();
		if (sinks.wired) return;

		sinks.handlers.R = &R;

		sinks.onSendR   = D.sink<EvReSendR>().connect<&ReliabilityHandlers::OnSendR>(&sinks.handlers);
		sinks.onRecvR   = D.sink<EvReRecvR>().connect<&ReliabilityHandlers::OnRecvR>(&sinks.handlers);
		sinks.onRecvACK = D.sink<EvReRecvACK>().connect<&ReliabilityHandlers::OnRecvACK>(&sinks.handlers);
		sinks.onRecvNACK= D.sink<EvReRecvNACK>().connect<&ReliabilityHandlers::OnRecvNACK>(&sinks.handlers);

		sinks.wired = true;
	}

	inline void ReliabilityTickSystem(utils::exec::ShardLocal& L, uint64 now_ns, uint64 dt_ns)
	{
		auto& R = L.world;
		auto& pools = R.ctx().get<EcsHandlePools>();
		const uint64 now = utils::Clock::Instance().NowNs();

		auto view = R.view<CompReliability, CompEndpoint>();
		for (auto e : view)
		{
			auto& cr = view.get<CompReliability>(e);
			auto& ep = view.get<CompEndpoint>(e);
			auto* st = pools.reliability.get(cr.hStore);
			if (!st) continue;

			// 펜딩 없을 때 지연 ACK (타임아웃)
			if (st->pending.empty())
			{
				if (cr.hasPendingAck && (now - cr.firstPendingAckTime_ns) >= MAX_DELAY_TICK_PIGGYBACK_ACK)
				{
					ecs::EnqueueSend(R, e,
						PacketBuilder::CreateReliabilityAckPacket(cr.pendingAckSeq, cr.pendingAckBitfield),
						eTxReason::ACK_ONLY);
					cr.hasPendingAck = false;
					cr.pendingAckSeq = 0;
					cr.pendingAckBitfield = 0;
					cr.firstPendingAckTime_ns = 0;
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
					// 손실 통계: Netstat ECS 이벤트 사용 시 별도 trigger 가능
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
					cr.inFlightSize -= it->second.size;
					st->pending.erase(it);
				}
			}

			for (auto seq : toRTX)
			{
				if (auto it = st->pending.find(seq); it != st->pending.end())
				{
					ecs::EnqueueSend(R, e, it->second.buffer, eTxReason::RETRANSMIT);
					it->second.retryCount++;
					it->second.timestamp = now;

					R.ctx().get<entt::dispatcher>().enqueue<EvNsOnFastRTX>(EvNsOnFastRTX{ e });
					R.ctx().get<entt::dispatcher>().enqueue<EvCCFastRTX>(EvCCFastRTX{ e });
				}
			}

			// 펜딩 있어도 ACK 지연 한도 초과 -> 독립 ACK 송신
			if (cr.hasPendingAck && (now - cr.firstPendingAckTime_ns) >= MAX_DELAY_TICK_PIGGYBACK_ACK)
			{
				ecs::EnqueueSend(R, e,
					PacketBuilder::CreateReliabilityAckPacket(cr.pendingAckSeq, cr.pendingAckBitfield),
					eTxReason::ACK_ONLY);
				cr.hasPendingAck = false;
				cr.pendingAckSeq = 0;
				cr.pendingAckBitfield = 0;
				cr.firstPendingAckTime_ns = 0;
			}
		}
	}
}
