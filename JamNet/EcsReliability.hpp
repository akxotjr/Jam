#pragma once

#include "pch.h"

#include "EcsCommon.hpp"
#include "EcsCongestionControl.hpp"
#include "EcsHandle.h"
#include "EcsNetstat.hpp"
#include "PacketBuilder.h"
#include "ReliableTransportManager.h"

namespace jam::net::ecs
{
	struct EvNsOnFastRTX;
	struct EvCCFastRTX;
	struct CompEndpoint;

	// Components

	struct CompReliability
	{
		// 송신/수신 창
		uint16						sendSeq = 1;
		uint16						latestSeq = 0;
		uint16						expectedNextSeq = 1;
		std::bitset<WINDOW_SIZE>	receiveHistory;

		// in-flight(바이트) / 지연 ACK
		uint32						inFlightSize = 0;
		bool						hasPendingAck = false;
		uint16						pendingAckSeq = 0;
		uint32						pendingAckBitfield = 0;
		uint64						firstPendingAckTick = 0;

		// NACK 타이밍
		uint64						lastNackTime = 0;

		// 콜드 스토어 핸들
		EcsHandle					hStore = EcsHandle::invalid();
	};

	struct ReliabilityStore
	{
		xumap<uint16, PendingPacketInfo> pending;   // seq -> {buf,size,timestamp,retry}
		xumap<uint16, uint32>            dupAckCount;
		xuset<uint16>                    sentNackSeqs;
		uint16                           lastAckedSeq = 0;
	};

	// Events

	// SEND

	struct EvReSendR
	{
		entt::entity		e{ entt::null };
		Sptr<SendBuffer>	buf;
		uint16				seq;
		uint32				size;
		uint64				ts;
	};

	struct EvRePiggybackACK
	{
		entt::entity		e{ entt::null };
		Sptr<SendBuffer>	buf;
	};

	struct EvRePiggybackACKFailed
	{
		entt::entity		e{ entt::null };
	};

	
	// RECV
	// Reliabilty Recv Event
	struct EvReRecvR
	{
		entt::entity		e{ entt::null };
		uint16				seq;
	};

	struct EvReRecvACK
	{
		entt::entity		e{ entt::null };
		uint16				latestSeq{};
		uint32				bitfield{};
	};

	struct EvReRecvNACK
	{
		entt::entity		e{ entt::null };
		uint16				missingSeq;
		uint32				bitfield;
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

		void OnSendR(const EvReSendR& ev)
		{
			auto& pools = R->ctx().get<EcsHandlePools>();
			auto& cr = R->get<CompReliability>(ev.e);
			auto* st = pools.reliability.get(cr.hStore);
			if (!st) return;

			st->pending[ev.seq] = { ev.buf, ev.size, ev.ts, 0 };
			cr.inFlightSize += ev.size;

			auto& cc = R->get<CompCongestion>(ev.e);
			auto& ep = R->get<CompEndpoint>(ev.e);
			if (cc.cwnd < cr.inFlightSize)
			{
				ep.owner->PushSendQueue(ev.buf);
			}

			if (cr.hasPendingAck) 
			{
				// todo : trigger or enqueue
				R->ctx().get<entt::dispatcher>().trigger<EvRePiggybackACK>(EvRePiggybackACK{ ev.e, ev.buf });
			}

			ep.owner->SendDirect(ev.buf);
		}

		void OnPiggyBackACK(const EvRePiggybackACK& ev)
		{
			auto& cr = R->get<CompReliability>(ev.e);

			if (!cr.hasPendingAck || !ev.buf || !ev.buf->Buffer()) return;

			PacketHeader* pktHeader = reinterpret_cast<PacketHeader*>(ev.buf->Buffer());
			const uint16 currentSize = pktHeader->GetSize();
			const uint32 remaining = ev.buf->AllocSize() - currentSize;

			if (remaining < sizeof(AckHeader)) 
			{
				// failed event
				// todo : trigger or enqueue
				R->ctx().get<entt::dispatcher>().trigger<EvRePiggybackACKFailed>(EvRePiggybackACKFailed{ ev.e });
				return;
			}

			auto* ack = reinterpret_cast<AckHeader*>(ev.buf->Buffer() + currentSize);
			ack->latestSeq = cr.pendingAckSeq;
			ack->bitfield = cr.pendingAckBitfield;

			const uint16 newSize = static_cast<uint16>(currentSize + sizeof(AckHeader));
			pktHeader->SetSize(newSize);
			pktHeader->SetFlags(pktHeader->GetFlags() | PacketFlags::PIGGYBACK_ACK);

			ev.buf->Close(newSize);

			// 성공 → 보류 ACK 클리어
			cr.hasPendingAck = false;
			cr.pendingAckSeq = 0;
			cr.pendingAckBitfield = 0;
			cr.firstPendingAckTick = 0;
		}

		void OnPiggybackFailed(const EvRePiggybackACKFailed& ev)
		{
			auto&& [cr, ep] = R->get<CompReliability, CompEndpoint>(ev.e);
			const uint64 now = utils::Clock::Instance().NowNs();
			// 즉시 ACK 조건(지연 한도 초과)이면 독립 ACK 송신
			if (cr.hasPendingAck && (now - cr.firstPendingAckTick) >= MAX_DELAY_TICK_PIGGYBACK_ACK) 
			{
				ep.owner->SendDirect(PacketBuilder::CreateReliabilityAckPacket(cr.pendingAckSeq, cr.pendingAckBitfield));
				cr.hasPendingAck = false;
				cr.pendingAckSeq = 0;
				cr.pendingAckBitfield = 0;
				cr.firstPendingAckTick = 0;
			}
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

			// gap → NACK 고려 (ShouldSendNack와 동등)
			if (SeqGreater(seq, cr.expectedNextSeq))
			{
				const uint64 now = jam::utils::Clock::Instance().NowNs();
				if ((now - cr.lastNackTime) >= jam::net::ReliableTransportManager::NACK_THROTTLE_INTERVAL &&
					!st->sentNackSeqs.contains(cr.expectedNextSeq) &&
					SeqGreater(seq, static_cast<uint16>(cr.expectedNextSeq + 1))) 
				{
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
			const uint64 now = jam::utils::Clock::Instance().NowNs();
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

		void OnRecvACK(const EvReRecvACK& ev)
		{
			auto& pools = R->ctx().get<EcsHandlePools>();
			auto [cr, ep] = R->get<CompReliability, CompEndpoint>(ev.e);
			auto* st = pools.reliability.get(cr.hStore);
			if (!st) return;

			// 확인된 펜딩 제거
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

			// Fast RTX (중복 ACK)
			if (!SeqGreater(ev.latestSeq, st->lastAckedSeq))
			{
				auto& cnt = st->dupAckCount[ev.latestSeq];
				if (++cnt >= 3)
				{
					const uint16 missingSeq = static_cast<uint16>(ev.latestSeq + 1);
					if (auto it = st->pending.find(missingSeq); it != st->pending.end())
					{
						ep.owner->SendDirect(it->second.buffer);
						it->second.retryCount++;
						it->second.timestamp = utils::Clock::Instance().NowNs();

						// todo: trigger or enqueue
						R->ctx().get<entt::dispatcher>().trigger<EvNsOnFastRTX>(EvNsOnFastRTX{ ev.e });
						R->ctx().get<entt::dispatcher>().trigger<EvCCFastRTX>(EvCCFastRTX{ ev.e });
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
						ep.owner->SendDirect(it->second.buffer);
						it->second.retryCount++;
						it->second.timestamp = jam::utils::Clock::Instance().NowNs();

						// todo: trigger or enqueue
						R->ctx().get<entt::dispatcher>().trigger<EvNsOnFastRTX>(EvNsOnFastRTX{ ev.e });
						R->ctx().get<entt::dispatcher>().trigger<EvCCFastRTX>(EvCCFastRTX{ ev.e });
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
		bool					wired = false;

		entt::scoped_connection onSendR;
		entt::scoped_connection onPiggyBack;
		entt::scoped_connection onPiggyFail;

		entt::scoped_connection onRecvR;
		entt::scoped_connection onRecvACK;
		entt::scoped_connection onRecvNACK;


		ReliabilityHandlers		handlers;
	};

	// Systems

	inline void ReliabilityWiringSystem(utils::exec::ShardLocal& L, uint64, uint64)
	{
		auto& R = L.world;
		auto& D = L.events;

		auto& sinks = R.ctx().emplace<ReliabilitySinks>();

		if (sinks.wired) return;

		sinks.handlers.R = &R; // 핸들러에 레지스트리 주입

		sinks.onSendR		= D.sink<EvReSendR>().connect<&ReliabilityHandlers::OnSendR>(&sinks.handlers);
		sinks.onPiggyBack	= D.sink<EvRePiggybackACK>().connect<&ReliabilityHandlers::OnPiggyBackACK>(&sinks.handlers);
		sinks.onPiggyFail	= D.sink<EvRePiggybackACKFailed>().connect<&ReliabilityHandlers::OnPiggybackFailed>(&sinks.handlers);

		sinks.onRecvR		= D.sink<EvReRecvR>().connect<&ReliabilityHandlers::OnRecvR>(&sinks.handlers);
		sinks.onRecvACK		= D.sink<EvReRecvACK>().connect<&ReliabilityHandlers::OnRecvACK>(&sinks.handlers);
		sinks.onRecvNACK	= D.sink<EvReRecvNACK>().connect<&ReliabilityHandlers::OnRecvNACK>(&sinks.handlers);

		sinks.wired = true;
	}

	inline void ReliabilityTickSystem(utils::exec::ShardLocal& L, uint64 now_ns, uint64 dt_ns)
	{
		auto& R = L.world;
		auto& pools = R.ctx().get<EcsHandlePools>();
		const uint64 now = jam::utils::Clock::Instance().NowNs();

		auto view = R.view<CompReliability, CompEndpoint>();
		for (auto e : view)
		{
			auto& cr = view.get<CompReliability>(e);
			auto& ep = view.get<CompEndpoint>(e);
			auto* st = pools.reliability.get(cr.hStore);
			if (!st) continue;

			if (st->pending.empty())
			{
				// 지연 ACK 즉시 전송 판단
				if (cr.hasPendingAck && (now - cr.firstPendingAckTick) >= MAX_DELAY_TICK_PIGGYBACK_ACK)
				{
					ep.owner->SendDirect(PacketBuilder::CreateReliabilityAckPacket(cr.pendingAckSeq, cr.pendingAckBitfield));
					cr.hasPendingAck = false; cr.pendingAckSeq = 0; cr.pendingAckBitfield = 0; cr.firstPendingAckTick = 0;
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
					cr.inFlightSize -= it->second.size;
					st->pending.erase(it);
				}
			}

			for (auto seq : toRTX) 
			{
				if (auto it = st->pending.find(seq); it != st->pending.end())
				{
					ep.owner->SendDirect(it->second.buffer);
					it->second.retryCount++;
					it->second.timestamp = now;
					// todo: trigger or enqueue
					R.ctx().get<entt::dispatcher>().trigger<EvNsOnFastRTX>(EvNsOnFastRTX{ e });
					R.ctx().get<entt::dispatcher>().trigger<EvCCFastRTX>(EvCCFastRTX{ e });
				}
			}
			// 원본: Update()의 RTO/RTX 동작을 그대로 반영.

			// (옵션) 펜딩이 있어도 지연 ACK 임계면 플러시
			if (cr.hasPendingAck && (now - cr.firstPendingAckTick) >= MAX_DELAY_TICK_PIGGYBACK_ACK)
			{
				ep.owner->SendDirect(PacketBuilder::CreateReliabilityAckPacket(cr.pendingAckSeq, cr.pendingAckBitfield));
				cr.hasPendingAck = false; cr.pendingAckSeq = 0; cr.pendingAckBitfield = 0; cr.firstPendingAckTick = 0;
			}
		}
	}

}
