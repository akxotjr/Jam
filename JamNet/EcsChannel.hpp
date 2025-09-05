#pragma once

#include "pch.h"
#include "ChannelManager.h"
#include "EcsHandle.h"


namespace jam::net::ecs
{
	// Components

	struct CompChannel
	{
		std::array<uint16, 4> latestSeqs{ 0 };  // Sequenced
		std::array<uint16, 4> expectedSeqs{ 1 }; // Ordered
		EcsHandle hStore = EcsHandle::invalid();
	};

	struct ChannelStore
	{
		std::array<std::map<uint16, std::pair<std::vector<BYTE>, PacketAnalysis>>, 4> buffers;
		static constexpr uint64 ORDERED_BUFFER_TIMEOUT = 100;
		static constexpr int16  MAX_SEQUENCE_GAP = 100;
	};

	// Events
	struct EvChannelRecv
	{
		entt::entity e;
		PacketAnalysis analysis;
		BYTE* payload;
		uint32 payloadSize;
	};

	// Handlers
	struct ChannelHandlers
	{
        entt::registry* R{};

        bool IsReliable(eChannelType ch) const;
        bool IsOrdered(eChannelType ch)  const;
        bool IsSequenced(eChannelType ch)const;

        void OnChannelRecv(const EvChannelRecv& ev) {
            auto& Rg = *R;
            auto& pools = Rg.ctx().get<EcsHandlePools>();
            auto [cc, ep] = Rg.get<CompChannel, CompEndpoint>(ev.e);
            auto* store = pools.channels.get(cc.hStore); // EcsHandlePools::channels 추가 가정
            if (!store) return;

            const auto ch = ev.analysis.GetChannel();
            const auto idx = E2U(ch);

            if (IsSequenced(ch)) {
                uint16 cur = ev.analysis.GetSequence();
                uint16& latest = cc.latestSeqs[idx];
                if (static_cast<int16>(cur - latest) > 0) latest = cur;
                else return; // 오래된 패킷 드롭
            }

            if (!IsOrdered(ch)) {
                // unordered -> 바로 상층에 전달
                // ep.owner->ProcessBufferedPacket(...) 필요 시
                return;
            }

            // Ordered
            uint16 cur = ev.analysis.GetSequence();
            uint16& expected = cc.expectedSeqs[idx];

            if (cur == expected) {
                ++expected;
                // 버퍼에 쌓인 연속 구간을 처리
                ProcessBuffered(cc, *store, ep.owner, ch);
            }
            else if (static_cast<int16>(cur - expected) > 0) {
                // 미래: 버퍼링
                BufferOrdered(*store, ev.analysis, ev.payload, ev.payloadSize);
            }
            else {
                // 과거: 무시
            }
        }

        // 내부 유틸
        void BufferOrdered(ChannelOrderedStore& st, const PacketAnalysis& an, BYTE* payload, uint32 sz) {
            auto ch = an.GetChannel();
            auto idx = E2U(ch);
            std::vector<BYTE> temp(payload, payload + sz);
            st.buffers[idx][an.GetSequence()] = std::make_pair(std::move(temp), an);

            const uint32 maxBuf = CHANNEL_CONFIGS[idx].maxQueueSize; // from ChannelManager.h
            if (st.buffers[idx].size() > maxBuf) {
                st.buffers[idx].erase(st.buffers[idx].begin());
            }
        }

        void ProcessBuffered(CompChannel& cc, ChannelOrderedStore& st, UdpSession* owner, eChannelType ch) {
            auto idx = E2U(ch);
            auto& buf = st.buffers[idx];
            auto& expected = cc.expectedSeqs[idx];

            while (true) {
                auto it = buf.find(expected);
                if (it == buf.end()) break;
                const auto& [payload, an] = it->second;
                owner->ProcessBufferedPacket(an, const_cast<BYTE*>(payload.data()), (uint32)payload.size());
                buf.erase(it);
                ++expected;
            }
        }
	};

	// Sinks

    struct ChannelSinks
	{
        bool wired = false;
        entt::scoped_connection onRecv;
        ChannelHandlers handlers;
    };

    // Systems

    inline void ChannelWiringSystem(utils::exec::ShardLocal& L, uint64, uint64)
	{
        auto& R = L.world; auto& D = L.events;
        auto& sinks = R.ctx().emplace<ChannelSinks>();
        if (sinks.wired) return;
        sinks.handlers.R = &R;
        sinks.onRecv = D.sink<EvChannelRecv>().connect<&ChannelHandlers::OnChannelRecv>(&sinks.handlers);
        sinks.wired = true;
    }

    inline void ChannelTickSystem(utils::exec::ShardLocal& L, uint64, uint64) {
        auto& R = L.world;
        auto& pools = R.ctx().get<EcsHandlePools>();
        uint64 now = utils::Clock::Instance().GetCurrentTick();

        auto view = R.view<CompChannel, CompEndpoint>();
        for (auto e : view) 
        {
            auto& cc = view.get<CompChannel>(e);
            auto& ep = view.get<CompEndpoint>(e);
            auto* st = pools.channels.get(cc.hStore);
            if (!st) continue;

            // CheckOrderedBufferTimeout + ForceProcessOrderedBuffer 동작
            for (uint8 i = 0; i < 4; i++) 
            {
                auto ch = U2E(eChannelType, i);
                if (!CHANNEL_CONFIGS[i].ordered) 
                    continue;
                auto& buf = st->buffers[i];
                if (buf.empty()) 
                    continue;

                auto oldest = buf.begin();
                uint64 elapsed = now - oldest->second.second.timestamp;
                if (elapsed >= ChannelOrderedStore::ORDERED_BUFFER_TIMEOUT) 
                {
                    // MAX_SEQUENCE_GAP 고려하여 당겨 처리
                    uint64 processed = 0, maxN = 10;
                    auto& expected = cc.expectedSeqs[i];
                    while (!buf.empty() && processed < maxN) 
                    {
                        auto next = buf.begin();
                        uint16 seq = next->first;
                        if (static_cast<int16>(seq - expected) > ChannelOrderedStore::MAX_SEQUENCE_GAP) 
                        {
                            expected = (uint16)(seq + 1);
                        }
                        const auto& [payload, an] = next->second;
                        ep.owner->ProcessBufferedPacket(an, const_cast<BYTE*>(payload.data()), (uint32)payload.size());
                        buf.erase(next);
                        ++processed;
                        if (seq >= expected) 
                            expected = (uint16)(seq + 1);
                    }
                }
            }
        }
    }
}
