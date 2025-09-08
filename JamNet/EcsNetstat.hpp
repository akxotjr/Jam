#pragma once
#include "pch.h"
#include "NetStatManager.h"	// temp

namespace jam::net::ecs
{
	// Components	: c

	struct CompNetstat
	{
		NetStat		stat{};
		uint64		prevTick = 0;
		double		prevRtt = 0.0;
		uint64		bwSendAccum = 0;
		uint64		bwRecvAccum = 0;
		uint64		expectedRecv = 0;
		uint64		highestSeqSeen = 0;
		uint64		lastAckedSeq = 0;
		uint64		ackSendAccum = 0;
	};


	// Events	: ev

	struct EvNsOnSend
	{
		entt::entity	e{ entt::null };
		uint32			size;
	};

	struct EvNsOnRecv
	{
		entt::entity	e{ entt::null };
		uint32			size;
	};

	struct EvNsOnSendR
	{
		entt::entity	e{ entt::null };
	};

	struct EvNsOnRecvR
	{
		entt::entity	e{ entt::null };
		uint16			seq;
	};

	struct EvNsOnPacketLoss
	{
		entt::entity	e{ entt::null };
		uint32			count;
	};

	struct EvNsOnPiggyAck
	{
		entt::entity	e{ entt::null };
	};
	struct EvNsOnImmAck
	{
		entt::entity	e{ entt::null };
	};
	struct EvNsOnDelayedAck
	{
		entt::entity	e{ entt::null };
	};
	struct EvNsOnRTO
	{
		entt::entity	e{ entt::null };
	};
	struct EvNsOnFastRTX
	{
		entt::entity	e{ entt::null };
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
		bool					wired = false;

		entt::scoped_connection onSend;
		entt::scoped_connection onRecv;
		entt::scoped_connection onSendR;
		entt::scoped_connection onRecvR;
		entt::scoped_connection onPacketLoss;
		entt::scoped_connection onPiggybackACK;
		entt::scoped_connection onImmdediateACK;
		entt::scoped_connection onDelayedACK;
		entt::scoped_connection onRTO;
		entt::scoped_connection onFastRTX;

		NetstatHandlers			handlers;
	};


	// Systems

	inline void NetstatWiringSystem(utils::exec::ShardLocal& L, uint64, uint64)
	{
		auto& R = L.world;
		auto& D = L.events;
		auto& sinks = R.ctx().emplace<NetstatSinks>();
		if (sinks.wired) return;
		sinks.handlers.R = &R;

		sinks.onSend			= D.sink<EvNsOnSend>().connect<&NetstatHandlers::OnSend>(&sinks.handlers);
		sinks.onRecv			= D.sink<EvNsOnRecv>().connect<&NetstatHandlers::OnRecv>(&sinks.handlers);
		sinks.onSendR			= D.sink<EvNsOnSendR>().connect<&NetstatHandlers::OnSendR>(&sinks.handlers);
		sinks.onRecvR			= D.sink<EvNsOnRecvR>().connect<&NetstatHandlers::OnRecvAck>(&sinks.handlers);
		sinks.onPacketLoss		= D.sink<EvNsOnPacketLoss>().connect<&NetstatHandlers::OnPacketLoss>(&sinks.handlers);
		sinks.onPiggybackACK	= D.sink<EvNsOnPiggyAck>().connect<&NetstatHandlers::OnPiggy>(&sinks.handlers);
		sinks.onImmdediateACK	= D.sink<EvNsOnImmAck>().connect<&NetstatHandlers::OnImm>(&sinks.handlers);
		sinks.onDelayedACK		= D.sink<EvNsOnDelayedAck>().connect<&NetstatHandlers::OnDelayed>(&sinks.handlers);
		sinks.onRTO				= D.sink<EvNsOnRTO>().connect<&NetstatHandlers::OnRTO>(&sinks.handlers);
		sinks.onFastRTX			= D.sink<EvNsOnFastRTX>().connect<&NetstatHandlers::OnFast>(&sinks.handlers);

		sinks.wired = true;
	}

	inline void NetstatTickSystem(utils::exec::ShardLocal& L, uint64 now_ns, uint64 dt_ns)
	{
		auto& R = L.world;
		auto view = R.view<CompNetstat>();
		for (auto e : view) 
		{
			auto& cn = view.get<CompNetstat>(e);
			// UpdateBandwidth
			cn.stat.bandwidthSend = static_cast<float>(cn.bwSendAccum / NetStatManager::TICK_INTERVAL);			//todo:TICK_INTERVAL 
			cn.stat.bandwidthRecv = static_cast<float>(cn.bwRecvAccum / NetStatManager::TICK_INTERVAL);			//todo:TICK_INTERVAL 
			cn.bwSendAccum = 0; cn.bwRecvAccum = 0;

			if (cn.stat.totalSent > 0) 
			{
				cn.stat.packetLossSend = static_cast<float>(cn.stat.totalLost) / static_cast<float>(cn.stat.totalSent) * 100.f;
			}
			if (cn.expectedRecv > 0 && cn.stat.totalRecv > 0) 
			{
				cn.stat.packetLossRecv = static_cast<float>(cn.expectedRecv - cn.stat.totalRecv) / static_cast<float>(cn.expectedRecv) * 100.f;
				cn.stat.packetLossRecv = std::clamp(cn.stat.packetLossRecv, 0.f, 100.f);
			}

			// UpdateAckEfficiency
			if (cn.bwSendAccum > 0) 
			{
				float ackBw = static_cast<float>(cn.ackSendAccum / NetStatManager::TICK_INTERVAL);
				cn.stat.ackEfficiency = ackBw / cn.stat.bandwidthSend;
			}
			else
			{
				cn.stat.ackEfficiency = 0.f;
			}

			cn.ackSendAccum = 0;

			// UpdateRetransmitStats
			if (cn.stat.totalRetransmits > 0) 
			{
				cn.stat.fastRetransmitRatio = static_cast<float>(cn.stat.fastRetransmits) / static_cast<float>(cn.stat.totalRetransmits);
			}
		}
	}
}

