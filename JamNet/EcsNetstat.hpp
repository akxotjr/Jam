#pragma once
#include "pch.h"

namespace jam::net::ecs
{
	// ä�� ���� (Channel / Packet ���ǿ� ��ġ�ϵ��� ����)
	constexpr uint32 NET_MAX_CHANNELS = 4;

	// ä�� ���(���� ChannelStat ������ : NetStatManager ����)
	struct ChannelStat
	{
		uint64 totalSent = 0;
		uint64 totalRecv = 0;
		uint64 totalLost = 0;        // reliable
		uint64 totalDropped = 0;     // sequenced ������ ��Ŷ ���
		uint64 totalBuffered = 0;    // ordered ���� ����
		uint64 totalReordered = 0;   // ordered ���� flush ó���� ����
		uint64 averageBufferDelay_ns = 0;
		float  utilization = 0.0f;   // (ä�� �۽� �뿪�� / ��ü �۽� �뿪��)
	};

	struct ChannelRuntime
	{
		ChannelStat stat;
		uint64 bwSendAccum = 0;          // ƽ ���� �۽� bytes
		uint64 bwRecvAccum = 0;          // ƽ ���� ���� bytes
		uint64 bufferDelayAccum_ns = 0;
		uint64 bufferedPackets = 0;
	};

	// ���� ���� ��Ʈ��ũ ��� ������Ʈ (���� ����)
	struct CompNetstat
	{
		// ----- �⺻ ���� -----
		uint64 totalSent = 0;
		uint64 totalRecv = 0;
		uint64 totalLost = 0;

		// ACK
		uint64 totalAcksSend = 0;
		uint64 totalAcksRecv = 0;
		uint64 piggyAcks = 0;
		uint64 immAcks = 0;
		uint64 delayedAcks = 0;

		// ������
		uint64 timeoutRetransmits = 0;
		uint64 fastRetransmits = 0;      // fast + nack ����
		uint64 totalRetransmits = 0;

		// RTT / ���� (ns ����)
		uint64 rtt_ns = 0;
		uint64 jitter_ns = 0;
		uint64 rttVariance = 0;
		uint64 prevRtt_ns = 0;
		uint64 rttProbeSend_ns = 0;      // RTT ���� �۽� �ð�

		// �Ļ� ��
		float  bandwidthSend_Bps = 0.f;
		float  bandwidthRecv_Bps = 0.f;
		float  packetLossSend_pct = 0.f;
		float  packetLossRecv_pct = 0.f;
		float  ackEfficiency = 0.f;
		float  fastRetransmitRatio = 0.f;

		// �ս� ����
		uint64 expectedRecv = 0;
		uint64 highestSeqSeen = 0;
		uint64 lastAckedSeq = 0;

		// ƽ ���� (�뿪��/ACK ũ��)
		uint64 accSendBytes = 0;
		uint64 accRecvBytes = 0;
		uint64 accAckBytes = 0;

		// ä��
		std::array<ChannelRuntime, NET_MAX_CHANNELS> channels{};
	};

	// ----- �̺�Ʈ (�ܺ� ���� �ڵ� ���� �ּ�ȭ ���� ���� ��� ����) -----
	struct EvNsOnSend { entt::entity e{ entt::null }; uint32 size; };
	struct EvNsOnRecv { entt::entity e{ entt::null }; uint32 size; };
	struct EvNsOnSendR { entt::entity e{ entt::null }; };          // RTT probe �۽�
	struct EvNsOnRecvR { entt::entity e{ entt::null }; uint16 seq; }; // RTT probe ACK ����
	struct EvNsOnPacketLoss { entt::entity e{ entt::null }; uint32 count; };
	struct EvNsOnPiggyAck { entt::entity e{ entt::null }; };
	struct EvNsOnImmAck { entt::entity e{ entt::null }; };
	struct EvNsOnDelayedAck { entt::entity e{ entt::null }; };
	struct EvNsOnRTO { entt::entity e{ entt::null }; };
	struct EvNsOnFastRTX { entt::entity e{ entt::null }; }; // fast + nack RTX ����
	// (EvNsOnNackRTX ����)

	// ä�� �̺�Ʈ
	struct EvNsChOnSend { entt::entity e{ entt::null }; uint8 ch; uint32 size; };
	struct EvNsChOnRecv { entt::entity e{ entt::null }; uint8 ch; uint32 size; };
	struct EvNsChOnDrop { entt::entity e{ entt::null }; uint8 ch; uint32 count; };
	struct EvNsChOnLoss { entt::entity e{ entt::null }; uint8 ch; uint32 count; };
	struct EvNsChOnBuffered { entt::entity e{ entt::null }; uint8 ch; uint64 delay_ns; };
	struct EvNsChOnReordered { entt::entity e{ entt::null }; uint8 ch; uint32 count; };

	// ----- �ڵ鷯 -----
	struct NetstatHandlers
	{
		entt::registry* R{};

		// ���� ���� EWMA (�� = 1/10)
		static inline uint64 IIR(uint64 prev, uint64 sample, uint32 num = 1, uint32 den = 10)
		{
			return (prev == 0) ? sample : ((prev * (den - num) + sample * num) / den);
		}

		void OnSend(const EvNsOnSend& ev)
		{
			auto& s = R->get<CompNetstat>(ev.e);
			s.totalSent++;
			s.accSendBytes += ev.size;
		}
		void OnRecv(const EvNsOnRecv& ev)
		{
			auto& s = R->get<CompNetstat>(ev.e);
			s.totalRecv++;
			s.accRecvBytes += ev.size;
		}
		void OnSendR(const EvNsOnSendR& ev)
		{
			R->get<CompNetstat>(ev.e).rttProbeSend_ns = utils::Clock::Instance().NowNs();
		}
		void OnRecvAck(const EvNsOnRecvR& ev)
		{
			auto& s = R->get<CompNetstat>(ev.e);
			if (!s.rttProbeSend_ns) return;
			const uint64 now = utils::Clock::Instance().NowNs();
			const uint64 sample = now - s.rttProbeSend_ns;

			// RTT / Jitter / Variance
			s.rtt_ns = IIR(s.rtt_ns, sample);
			const uint64 diffAbs = (s.rtt_ns > sample) ? (s.rtt_ns - sample) : (sample - s.rtt_ns);
			s.jitter_ns = IIR(s.jitter_ns, diffAbs);
			const uint64 rttDiff = (s.rtt_ns > s.prevRtt_ns) ? (s.rtt_ns - s.prevRtt_ns) : (s.prevRtt_ns - s.rtt_ns);
			s.rttVariance = IIR(s.rttVariance, rttDiff * rttDiff);
			s.prevRtt_ns = s.rtt_ns;

			// ����/�ս� ���
			s.lastAckedSeq = ev.seq;
			if (ev.seq > s.highestSeqSeen) 
			{
				s.expectedRecv += (ev.seq - s.highestSeqSeen);
				s.highestSeqSeen = ev.seq;
			}
			s.totalAcksRecv++;
		}
		void OnPacketLoss(const EvNsOnPacketLoss& ev)
		{
			R->get<CompNetstat>(ev.e).totalLost += ev.count;
		}
		void OnPiggy(const EvNsOnPiggyAck& ev)
		{
			auto& s = R->get<CompNetstat>(ev.e);
			s.piggyAcks++;
			s.totalAcksSend++;
			s.accAckBytes += sizeof(AckHeader);
		}
		void OnImm(const EvNsOnImmAck& ev) {
			auto& s = R->get<CompNetstat>(ev.e);
			s.immAcks++;
			s.totalAcksSend++;
			s.accAckBytes += sizeof(PacketHeader) + sizeof(AckHeader);
		}
		void OnDelayed(const EvNsOnDelayedAck& ev) {
			R->get<CompNetstat>(ev.e).delayedAcks++;
		}
		void OnRTO(const EvNsOnRTO& ev) {
			auto& s = R->get<CompNetstat>(ev.e);
			s.timeoutRetransmits++;
			s.totalRetransmits++;
		}
		void OnFast(const EvNsOnFastRTX& ev) {
			auto& s = R->get<CompNetstat>(ev.e);
			s.fastRetransmits++;
			s.totalRetransmits++;
		}

		// ä��
		void ChSend(const EvNsChOnSend& ev) {
			if (ev.ch >= NET_MAX_CHANNELS) return;
			auto& rt = R->get<CompNetstat>(ev.e).channels[ev.ch];
			rt.stat.totalSent++;
			rt.bwSendAccum += ev.size;
		}
		void ChRecv(const EvNsChOnRecv& ev) {
			if (ev.ch >= NET_MAX_CHANNELS) return;
			auto& rt = R->get<CompNetstat>(ev.e).channels[ev.ch];
			rt.stat.totalRecv++;
			rt.bwRecvAccum += ev.size;
		}
		void ChDrop(const EvNsChOnDrop& ev) {
			if (ev.ch >= NET_MAX_CHANNELS) return;
			R->get<CompNetstat>(ev.e).channels[ev.ch].stat.totalDropped += ev.count;
		}
		void ChLoss(const EvNsChOnLoss& ev) {
			if (ev.ch >= NET_MAX_CHANNELS) return;
			R->get<CompNetstat>(ev.e).channels[ev.ch].stat.totalLost += ev.count;
		}
		void ChBuffered(const EvNsChOnBuffered& ev) {
			if (ev.ch >= NET_MAX_CHANNELS) return;
			auto& rt = R->get<CompNetstat>(ev.e).channels[ev.ch];
			rt.stat.totalBuffered++;
			rt.bufferDelayAccum_ns += ev.delay_ns;
			rt.bufferedPackets++;
		}
		void ChReordered(const EvNsChOnReordered& ev) {
			if (ev.ch >= NET_MAX_CHANNELS) return;
			R->get<CompNetstat>(ev.e).channels[ev.ch].stat.totalReordered += ev.count;
		}
	};

	// ----- ����(���̾) -----
	struct NetstatSinks
	{
		bool wired = false;

		entt::scoped_connection onSend;
		entt::scoped_connection onRecv;
		entt::scoped_connection onSendR;
		entt::scoped_connection onRecvR;
		entt::scoped_connection onPacketLoss;
		entt::scoped_connection onPiggy;
		entt::scoped_connection onImm;
		entt::scoped_connection onDelayed;
		entt::scoped_connection onRTO;
		entt::scoped_connection onFast; // NACK ����

		// ä��
		entt::scoped_connection chSend;
		entt::scoped_connection chRecv;
		entt::scoped_connection chDrop;
		entt::scoped_connection chLoss;
		entt::scoped_connection chBuffered;
		entt::scoped_connection chReordered;

		NetstatHandlers handlers;
	};

	inline void NetstatWiringSystem(utils::exec::ShardLocal& L, uint64, uint64)
	{
		auto& R = L.world;
		auto& D = L.events;
		auto& sinks = R.ctx().emplace<NetstatSinks>();
		if (sinks.wired) return;
		sinks.handlers.R = &R;

		sinks.onSend = D.sink<EvNsOnSend>().connect<&NetstatHandlers::OnSend>(&sinks.handlers);
		sinks.onRecv = D.sink<EvNsOnRecv>().connect<&NetstatHandlers::OnRecv>(&sinks.handlers);
		sinks.onSendR = D.sink<EvNsOnSendR>().connect<&NetstatHandlers::OnSendR>(&sinks.handlers);
		sinks.onRecvR = D.sink<EvNsOnRecvR>().connect<&NetstatHandlers::OnRecvAck>(&sinks.handlers);
		sinks.onPacketLoss = D.sink<EvNsOnPacketLoss>().connect<&NetstatHandlers::OnPacketLoss>(&sinks.handlers);
		sinks.onPiggy = D.sink<EvNsOnPiggyAck>().connect<&NetstatHandlers::OnPiggy>(&sinks.handlers);
		sinks.onImm = D.sink<EvNsOnImmAck>().connect<&NetstatHandlers::OnImm>(&sinks.handlers);
		sinks.onDelayed = D.sink<EvNsOnDelayedAck>().connect<&NetstatHandlers::OnDelayed>(&sinks.handlers);
		sinks.onRTO = D.sink<EvNsOnRTO>().connect<&NetstatHandlers::OnRTO>(&sinks.handlers);
		sinks.onFast = D.sink<EvNsOnFastRTX>().connect<&NetstatHandlers::OnFast>(&sinks.handlers);
		// NACK �̺�Ʈ ����

		// ä��
		sinks.chSend = D.sink<EvNsChOnSend>().connect<&NetstatHandlers::ChSend>(&sinks.handlers);
		sinks.chRecv = D.sink<EvNsChOnRecv>().connect<&NetstatHandlers::ChRecv>(&sinks.handlers);
		sinks.chDrop = D.sink<EvNsChOnDrop>().connect<&NetstatHandlers::ChDrop>(&sinks.handlers);
		sinks.chLoss = D.sink<EvNsChOnLoss>().connect<&NetstatHandlers::ChLoss>(&sinks.handlers);
		sinks.chBuffered = D.sink<EvNsChOnBuffered>().connect<&NetstatHandlers::ChBuffered>(&sinks.handlers);
		sinks.chReordered = D.sink<EvNsChOnReordered>().connect<&NetstatHandlers::ChReordered>(&sinks.handlers);

		sinks.wired = true;
	}

	// ----- ƽ �ý��� -----
	inline void NetstatTickSystem(utils::exec::ShardLocal& L, uint64, uint64 dt_ns)
	{
		if (dt_ns == 0) return;
		auto& R = L.world;
		const double invDt = 1'000'000'000.0 / static_cast<double>(dt_ns);

		auto view = R.view<CompNetstat>();
		for (auto e : view)
		{
			auto& s = view.get<CompNetstat>(e);

			// �뿪��
			s.bandwidthSend_Bps = static_cast<float>(s.accSendBytes * invDt);
			s.bandwidthRecv_Bps = static_cast<float>(s.accRecvBytes * invDt);

			// �սǷ� (�۽� ����)
			if (s.totalSent > 0)
				s.packetLossSend_pct = static_cast<float>(s.totalLost) / static_cast<float>(s.totalSent) * 100.f;

			// �սǷ� (���� ��� ���)
			if (s.expectedRecv > 0 && s.totalRecv > 0) 
			{
				float loss = static_cast<float>(s.expectedRecv - s.totalRecv) / static_cast<float>(s.expectedRecv) * 100.f;
				s.packetLossRecv_pct = std::clamp(loss, 0.f, 100.f);
			}

			// ACK ȿ��
			if (s.bandwidthSend_Bps > 0.f) 
			{
				float ackBw = static_cast<float>(s.accAckBytes * invDt);
				s.ackEfficiency = ackBw / s.bandwidthSend_Bps;
			}
			else
			{
				s.ackEfficiency = 0.f;
			}

			// ������ ����
			if (s.totalRetransmits > 0)
				s.fastRetransmitRatio = static_cast<float>(s.fastRetransmits) / static_cast<float>(s.totalRetransmits);
			else
				s.fastRetransmitRatio = 0.f;

			// ä��
			const float totalSendBw = s.bandwidthSend_Bps;
			for (auto& rt : s.channels)
			{
				const float chSendBw = static_cast<float>(rt.bwSendAccum * invDt);
				if (totalSendBw > 0.f)
					rt.stat.utilization = chSendBw / totalSendBw;
				else
					rt.stat.utilization = 0.f;

				if (rt.bufferedPackets > 0)
					rt.stat.averageBufferDelay_ns = rt.bufferDelayAccum_ns / rt.bufferedPackets;
				else
					rt.stat.averageBufferDelay_ns = 0;

				// tick accumulator reset
				rt.bwSendAccum = 0;
				rt.bwRecvAccum = 0;
				rt.bufferDelayAccum_ns = 0;
				rt.bufferedPackets = 0;
			}

			// ���� tick ���� ����
			s.accSendBytes = 0;
			s.accRecvBytes = 0;
			s.accAckBytes = 0;
		}
	}
}