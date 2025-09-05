#pragma once
#include "pch.h"
#include "NetStatManager.h"	// temp

namespace jam::net::ecs
{
	// Components	: c

	struct CompNetstat
	{
		NetStat stat{};
		uint64 prevTick = 0;
		double prevRtt = 0.0;
		uint64 bwSendAccum = 0;
		uint64 bwRecvAccum = 0;
		uint64 expectedRecv = 0;
		uint64 highestSeqSeen = 0;
		uint64 lastAckedSeq = 0;
		uint64 ackSendAccum = 0;
	};


	// Events	: ev

	struct EvNsOnSend
	{
		entt::entity	e;
		uint32			size;
	};

	struct EvNsOnRecv
	{
		entt::entity	e;
		uint32			size;
	};

	struct EvNsOnSendR
	{
		entt::entity	e;
	};

	struct EvNsOnRecvR
	{
		entt::entity	e;
		uint16			seq;
	};

	struct EvNsOnPacketLoss
	{
		entt::entity e;
		uint32 count;
	};

	struct EvNsOnPiggyAck
	{
		entt::entity	e;
	};
	struct EvNsOnImmAck
	{
		entt::entity	e;
	};
	struct EvNsOnDelayedAck
	{
		entt::entity e;
	};
	struct EvNsOnRTO
	{
		entt::entity e;
	};
	struct EvNsOnFastRTX
	{
		entt::entity e;
	};
	struct EvNsOnNackRTX
	{
		entt::entity e;
	};


	// Handler
	struct NetstatHandlers
	{
		entt::registry* R{};

		void OnSend(const EvNsOnSend& ev)
		{
			auto& s = R->get<CompNetstat>(ev.e);
			s.stat.totalSent++;
			s.bwSendAccum += ev.size;
		}

		void OnRecv(const EvNsOnRecv& ev)
		{
			auto& s = R->get<CompNetstat>(ev.e);
			s.stat.totalRecv++;
			s.bwRecvAccum += ev.size;
		}

		void OnSendR(const EvNsOnSendR& ev)
		{
			R->get<CompNetstat>(ev.e).prevTick = utils::Clock::Instance().GetCurrentTick();
		}

		void OnRecvAck(const EvNsOnRecvR& ev)
		{
			auto& ns = R->get<CompNetstat>(ev.e);
			uint64 now = utils::Clock::Instance().GetCurrentTick();
			uint64 rtt = now - ns.prevTick;
			ns.stat.rtt = 0.1 * rtt + 0.9 * ns.stat.rtt;
			ns.stat.jitter = 0.1 * std::abs((double)rtt - ns.stat.rtt) + 0.9 * ns.stat.jitter;
			if (ns.stat.rtt > 0) 
			{
				double diff = ns.stat.rtt - ns.prevRtt;
				ns.stat.rttVariance = 0.1 * (diff * diff) + 0.9 * ns.stat.rttVariance;
			}
			ns.prevRtt = ns.stat.rtt;

			ns.lastAckedSeq = ev.seq;
			if (ev.seq > ns.highestSeqSeen) 
			{
				ns.expectedRecv += (ev.seq - ns.highestSeqSeen);
				ns.highestSeqSeen = ev.seq;
			}
			ns.stat.totalAcksRecv++;
		}

		void OnPacketLoss(const EvNsOnPacketLoss& ev)
		{
			auto& s = R->get<CompNetstat>(ev.e);
			s.stat.totalLost += ev.count;
		}

		void OnPiggy(const EvNsOnPiggyAck& ev)
		{
			auto& s = R->get<CompNetstat>(ev.e);
			s.stat.piggybackAcks++;
			s.stat.totalAcksSend++;
			s.ackSendAccum += sizeof(AckHeader);
		}

		void OnImm(const EvNsOnImmAck& ev)
		{
			auto& s = R->get<CompNetstat>(ev.e);
			s.stat.immediateAcks++;
			s.stat.totalAcksSend++;
			s.ackSendAccum += sizeof(PacketHeader) + sizeof(AckHeader);
		}

		void OnDelayed(const EvNsOnDelayedAck& ev)
		{
			R->get<CompNetstat>(ev.e).stat.delayedAcks++;
		}

		void OnRTO(const EvNsOnRTO& ev)
		{
			auto& s = R->get<CompNetstat>(ev.e);
			s.stat.timeoutRetransmits++;
			s.stat.totalRetransmits++;
		}

		void OnFast(const EvNsOnFastRTX& ev)
		{
			auto& s = R->get<CompNetstat>(ev.e);
			s.stat.fastRetransmits++;
			s.stat.totalRetransmits++;
		}

		void OnNack(const EvNsOnNackRTX& ev)
		{
			auto& s = R->get<CompNetstat>(ev.e);
			s.stat.nackRetransmits++;
			s.stat.totalRetransmits++;
		}
	};

	// Sinks

	struct NetstatSinks
	{
		bool wired = false;
		entt::scoped_connection a, b, c, d, e, f, g, h, i, j, k;
		NetstatHandlers handlers;
	};


	// Systems

	inline void NetstatWiringSystem(utils::exec::ShardLocal& L, uint64, uint64)
	{
		auto& R = L.world;
		auto& D = L.events;
		auto& sinks = R.ctx().emplace<NetstatSinks>();
		if (sinks.wired) return;
		sinks.handlers.R = &R;

		sinks.a = D.sink<EvNsOnSend>().connect<&NetstatHandlers::OnSend>(&sinks.handlers);
		sinks.b = D.sink<EvNsOnRecv>().connect<&NetstatHandlers::OnRecv>(&sinks.handlers);
		sinks.c = D.sink<EvNsOnSendR>().connect<&NetstatHandlers::OnSendR>(&sinks.handlers);
		sinks.d = D.sink<EvNsOnRecvR>().connect<&NetstatHandlers::OnRecvAck>(&sinks.handlers);
		sinks.e = D.sink<EvNsOnPacketLoss>().connect<&NetstatHandlers::OnPacketLoss>(&sinks.handlers);
		sinks.f = D.sink<EvNsOnPiggyAck>().connect<&NetstatHandlers::OnPiggy>(&sinks.handlers);
		sinks.g = D.sink<EvNsOnImmAck>().connect<&NetstatHandlers::OnImm>(&sinks.handlers);
		sinks.h = D.sink<EvNsOnDelayedAck>().connect<&NetstatHandlers::OnDelayed>(&sinks.handlers);
		sinks.i = D.sink<EvNsOnRTO>().connect<&NetstatHandlers::OnRTO>(&sinks.handlers);
		sinks.j = D.sink<EvNsOnFastRTX>().connect<&NetstatHandlers::OnFast>(&sinks.handlers);
		sinks.k = D.sink<EvNsOnNackRTX>().connect<&NetstatHandlers::OnNack>(&sinks.handlers);

		sinks.wired = true;
	}

	inline void NetstatTickSystem(utils::exec::ShardLocal& L, uint64 now_ns, uint64 dt_ns)
	{
		auto& R = L.world;
		auto view = R.view<CompNetstat>();
		for (auto e : view) 
		{
			auto& s = view.get<CompNetstat>(e);
			// UpdateBandwidth
			s.stat.bandwidthSend = static_cast<float>(s.bwSendAccum / NetStatManager::TICK_INTERVAL);			//todo:TICK_INTERVAL 
			s.stat.bandwidthRecv = static_cast<float>(s.bwRecvAccum / NetStatManager::TICK_INTERVAL);			//todo:TICK_INTERVAL 
			s.bwSendAccum = 0; s.bwRecvAccum = 0;

			if (s.stat.totalSent > 0) 
			{
				s.stat.packetLossSend = (float)s.stat.totalLost / (float)s.stat.totalSent * 100.f;
			}
			if (s.expectedRecv > 0 && s.stat.totalRecv > 0) 
			{
				s.stat.packetLossRecv = (float)(s.expectedRecv - s.stat.totalRecv) / (float)s.expectedRecv * 100.f;
				s.stat.packetLossRecv = std::clamp(s.stat.packetLossRecv, 0.f, 100.f);
			}

			// UpdateAckEfficiency
			if (s.bwSendAccum > 0) 
			{
				float ackBw = (float)(s.ackSendAccum / NetStatManager::TICK_INTERVAL);
				s.stat.ackEfficiency = ackBw / s.stat.bandwidthSend;
			}
			else
			{
				s.stat.ackEfficiency = 0.f;
			}

			s.ackSendAccum = 0;

			// UpdateRetransmitStats
			if (s.stat.totalRetransmits > 0) 
			{
				s.stat.fastRetransmitRatio = (float)s.stat.fastRetransmits / (float)s.stat.totalRetransmits;
			}
		}
	}
}

