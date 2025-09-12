#pragma once
#include "pch.h"

namespace jam::net::ecs
{
	// 채널 개수 (Channel / Packet 정의와 일치하도록 유지)
	constexpr uint32 NET_MAX_CHANNELS = 4;

	// 채널 통계(기존 ChannelStat 재정의 : NetStatManager 제거)
	struct ChannelStat
	{
		uint64 totalSent = 0;
		uint64 totalRecv = 0;
		uint64 totalLost = 0;        // reliable
		uint64 totalDropped = 0;     // sequenced 오래된 패킷 폐기
		uint64 totalBuffered = 0;    // ordered 버퍼 적재
		uint64 totalReordered = 0;   // ordered 버퍼 flush 처리된 개수
		uint64 averageBufferDelay_ns = 0;
		float  utilization = 0.0f;   // (채널 송신 대역폭 / 전체 송신 대역폭)
	};

	struct ChannelRuntime
	{
		ChannelStat stat;
		uint64 bwSendAccum = 0;          // 틱 동안 송신 bytes
		uint64 bwRecvAccum = 0;          // 틱 동안 수신 bytes
		uint64 bufferDelayAccum_ns = 0;
		uint64 bufferedPackets = 0;
	};

	// 세션 단위 네트워크 통계 컴포넌트 (단일 권위)
	struct CompNetstat
	{
		// ----- 기본 누적 -----
		uint64 totalSent = 0;
		uint64 totalRecv = 0;
		uint64 totalLost = 0;

		// ACK
		uint64 totalAcksSend = 0;
		uint64 totalAcksRecv = 0;
		uint64 piggyAcks = 0;
		uint64 immAcks = 0;
		uint64 delayedAcks = 0;

		// 재전송
		uint64 timeoutRetransmits = 0;
		uint64 fastRetransmits = 0;      // fast + nack 통합
		uint64 totalRetransmits = 0;

		// RTT / 지터 (ns 단위)
		uint64 rtt_ns = 0;
		uint64 jitter_ns = 0;
		uint64 rttVariance = 0;
		uint64 prevRtt_ns = 0;
		uint64 rttProbeSend_ns = 0;      // RTT 샘플 송신 시각

		// 파생 값
		float  bandwidthSend_Bps = 0.f;
		float  bandwidthRecv_Bps = 0.f;
		float  packetLossSend_pct = 0.f;
		float  packetLossRecv_pct = 0.f;
		float  ackEfficiency = 0.f;
		float  fastRetransmitRatio = 0.f;

		// 손실 추정
		uint64 expectedRecv = 0;
		uint64 highestSeqSeen = 0;
		uint64 lastAckedSeq = 0;

		// 틱 누적 (대역폭/ACK 크기)
		uint64 accSendBytes = 0;
		uint64 accRecvBytes = 0;
		uint64 accAckBytes = 0;

		// 채널
		std::array<ChannelRuntime, NET_MAX_CHANNELS> channels{};
	};

	// ----- 이벤트 (외부 기존 코드 영향 최소화 위해 기존 명명 유지) -----
	struct EvNsOnSend { entt::entity e{ entt::null }; uint32 size; };
	struct EvNsOnRecv { entt::entity e{ entt::null }; uint32 size; };
	struct EvNsOnSendR { entt::entity e{ entt::null }; };          // RTT probe 송신
	struct EvNsOnRecvR { entt::entity e{ entt::null }; uint16 seq; }; // RTT probe ACK 수신
	struct EvNsOnPacketLoss { entt::entity e{ entt::null }; uint32 count; };
	struct EvNsOnPiggyAck { entt::entity e{ entt::null }; };
	struct EvNsOnImmAck { entt::entity e{ entt::null }; };
	struct EvNsOnDelayedAck { entt::entity e{ entt::null }; };
	struct EvNsOnRTO { entt::entity e{ entt::null }; };
	struct EvNsOnFastRTX { entt::entity e{ entt::null }; }; // fast + nack RTX 통합
	// (EvNsOnNackRTX 제거)

	// 채널 이벤트
	struct EvNsChOnSend { entt::entity e{ entt::null }; uint8 ch; uint32 size; };
	struct EvNsChOnRecv { entt::entity e{ entt::null }; uint8 ch; uint32 size; };
	struct EvNsChOnDrop { entt::entity e{ entt::null }; uint8 ch; uint32 count; };
	struct EvNsChOnLoss { entt::entity e{ entt::null }; uint8 ch; uint32 count; };
	struct EvNsChOnBuffered { entt::entity e{ entt::null }; uint8 ch; uint64 delay_ns; };
	struct EvNsChOnReordered { entt::entity e{ entt::null }; uint8 ch; uint32 count; };

	// ----- 핸들러 -----
	struct NetstatHandlers
	{
		entt::registry* R{};

		// 작은 정수 EWMA (α = 1/10)
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

			// 수신/손실 기대
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

		// 채널
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

	// ----- 연결(와이어링) -----
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
		entt::scoped_connection onFast; // NACK 포함

		// 채널
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
		// NACK 이벤트 제거

		// 채널
		sinks.chSend = D.sink<EvNsChOnSend>().connect<&NetstatHandlers::ChSend>(&sinks.handlers);
		sinks.chRecv = D.sink<EvNsChOnRecv>().connect<&NetstatHandlers::ChRecv>(&sinks.handlers);
		sinks.chDrop = D.sink<EvNsChOnDrop>().connect<&NetstatHandlers::ChDrop>(&sinks.handlers);
		sinks.chLoss = D.sink<EvNsChOnLoss>().connect<&NetstatHandlers::ChLoss>(&sinks.handlers);
		sinks.chBuffered = D.sink<EvNsChOnBuffered>().connect<&NetstatHandlers::ChBuffered>(&sinks.handlers);
		sinks.chReordered = D.sink<EvNsChOnReordered>().connect<&NetstatHandlers::ChReordered>(&sinks.handlers);

		sinks.wired = true;
	}

	// ----- 틱 시스템 -----
	inline void NetstatTickSystem(utils::exec::ShardLocal& L, uint64, uint64 dt_ns)
	{
		if (dt_ns == 0) return;
		auto& R = L.world;
		const double invDt = 1'000'000'000.0 / static_cast<double>(dt_ns);

		auto view = R.view<CompNetstat>();
		for (auto e : view)
		{
			auto& s = view.get<CompNetstat>(e);

			// 대역폭
			s.bandwidthSend_Bps = static_cast<float>(s.accSendBytes * invDt);
			s.bandwidthRecv_Bps = static_cast<float>(s.accRecvBytes * invDt);

			// 손실률 (송신 기준)
			if (s.totalSent > 0)
				s.packetLossSend_pct = static_cast<float>(s.totalLost) / static_cast<float>(s.totalSent) * 100.f;

			// 손실률 (수신 기대 기반)
			if (s.expectedRecv > 0 && s.totalRecv > 0) 
			{
				float loss = static_cast<float>(s.expectedRecv - s.totalRecv) / static_cast<float>(s.expectedRecv) * 100.f;
				s.packetLossRecv_pct = std::clamp(loss, 0.f, 100.f);
			}

			// ACK 효율
			if (s.bandwidthSend_Bps > 0.f) 
			{
				float ackBw = static_cast<float>(s.accAckBytes * invDt);
				s.ackEfficiency = ackBw / s.bandwidthSend_Bps;
			}
			else
			{
				s.ackEfficiency = 0.f;
			}

			// 재전송 비율
			if (s.totalRetransmits > 0)
				s.fastRetransmitRatio = static_cast<float>(s.fastRetransmits) / static_cast<float>(s.totalRetransmits);
			else
				s.fastRetransmitRatio = 0.f;

			// 채널
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

			// 전역 tick 누적 리셋
			s.accSendBytes = 0;
			s.accRecvBytes = 0;
			s.accAckBytes = 0;
		}
	}
}