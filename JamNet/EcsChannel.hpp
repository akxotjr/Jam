#pragma once
#include "pch.h"
#include "EcsHandle.h"


namespace jam::net::ecs
{
    // 구성 가능한 채널 수(고정 4 가정)
    constexpr uint32 CHANNEL_COUNT = 4;

    //static constexpr uint64 ORDERED_BUFFER_TIMEOUT = 100;
    //static constexpr int16 MAX_SEQUENCE_GAP = 100;

    struct ChannelConfig
    {
        bool reliable;
        bool ordered;
        bool sequenced;
        uint32 maxQueueSize;
    };

    constexpr std::array<ChannelConfig, 4> CHANNEL_CONFIGS = { {
        {false, false, false, 0},
        {true, true, false, 100},
        {false, false, true, 10},
        {true, false, false, 50}
    } };


    struct CompChannel
    {
        std::array<uint16, CHANNEL_COUNT> latestSeqs{ 0 };   // Sequenced 최신 수신 seq
        std::array<uint16, CHANNEL_COUNT> expectedSeqs{ 1 }; // Ordered 다음 기대 seq
        EcsHandle                         hStore = EcsHandle::invalid();
    };

    struct OrderedBufferedPacket
    {
        std::vector<BYTE>       payload;
        PacketAnalysis          analysis;
        uint64                  arrival_ns = 0_ns;
    };

    struct ChannelStore
    {
        std::array<std::map<uint16, OrderedBufferedPacket>, CHANNEL_COUNT> buffers;

        // 타임아웃 (예: 30ms) 실제 요구에 맞게 조정
        static constexpr uint64 ORDERED_BUFFER_TIMEOUT_NS = 3'000'000_ns;
        static constexpr int16  MAX_SEQUENCE_GAP = 100;
    };

    struct EvChRecv
    {
        entt::entity   e{ entt::null };
        PacketAnalysis analysis;
        BYTE*          payload = nullptr;
        uint32         payloadSize = 0;
    };

    struct ChannelHandlers
    {
        entt::registry* R{};

        bool IsReliable(eChannelType ch)  const;
        bool IsOrdered(eChannelType ch)   const;
        bool IsSequenced(eChannelType ch) const;

        void OnRecv(const EvChRecv& ev)
        {
            auto& pools = R->ctx().get<EcsHandlePools>();
            auto [cc, ep] = R->get<CompChannel, CompEndpoint>(ev.e);
            auto* store = pools.channels.get(cc.hStore);
            if (!store) return;

            const eChannelType ch  = ev.analysis.GetChannel();
            const uint32 idx       = E2U(ch);

            // Sequenced: 최신값만 갱신하고 과거 패킷 폐기
            if (IsSequenced(ch))
            {
                uint16 cur    = ev.analysis.GetSequence();
                uint16& latest = cc.latestSeqs[idx];
                if (static_cast<int16>(cur - latest) > 0)
                    latest = cur;
                else
                    return; // 과거/중복 → 폐기
            }

            // Ordered 아닌 경우 즉시 상위 전달 (필요 시 여기서 바로 세션에 전달)
            if (!IsOrdered(ch))
            {
                // ep.owner->ProcessBufferedPacket(ev.analysis, ev.payload, ev.payloadSize);
                return;
            }

            // Ordered 처리
            uint16 cur = ev.analysis.GetSequence();
            uint16& expected = cc.expectedSeqs[idx];

            if (cur == expected)
            {
                ++expected;
                // 뒤이어 연속된 버퍼 처리
                ProcessBuffered(cc, *store, ep.owner, ch);
            }
            else if (static_cast<int16>(cur - expected) > 0)
            {
                // 미래 seq → 버퍼링
                BufferOrdered(*store, ev.analysis, ev.payload, ev.payloadSize);
            }
            else
            {
                // 과거 → 폐기
            }
        }

        void BufferOrdered(ChannelStore& st, const PacketAnalysis& an, BYTE* payload, uint32 sz)
        {
            const auto ch  = an.GetChannel();
            const uint32 idx = E2U(ch);
            OrderedBufferedPacket obp;
            obp.payload.assign(payload, payload + sz);
            obp.analysis   = an;
            obp.arrival_ns = utils::Clock::Instance().NowNs();
            st.buffers[idx][an.GetSequence()] = std::move(obp);

            const uint32 maxBuf = CHANNEL_CONFIGS[idx].maxQueueSize;
            if (st.buffers[idx].size() > maxBuf)
            {
                // 가장 오래된 것 제거 (map 첫 요소)
                st.buffers[idx].erase(st.buffers[idx].begin());
            }
        }

        void ProcessBuffered(CompChannel& cc, ChannelStore& st, UdpSession* owner, eChannelType ch)
        {
            const uint32 idx = E2U(ch);
            auto& buf = st.buffers[idx];
            auto& expected = cc.expectedSeqs[idx];

            while (true)
            {
                auto it = buf.find(expected);
                if (it == buf.end()) break;

                auto& pkt = it->second;
                owner->ProcessBufferedPacket(pkt.analysis,
                    const_cast<BYTE*>(pkt.payload.data()),
                    static_cast<uint32>(pkt.payload.size()));
                buf.erase(it);
                ++expected;
            }
        }
    };

    struct ChannelSinks
    {
        bool                    wired = false;
        entt::scoped_connection onRecv;
        ChannelHandlers         handlers;
    };

    inline void ChannelWiringSystem(utils::exec::ShardLocal& L, uint64, uint64)
    {
        auto& R = L.world; auto& D = L.events;
        auto& sinks = R.ctx().emplace<ChannelSinks>();
        if (sinks.wired) return;
        sinks.handlers.R = &R;
        sinks.onRecv = D.sink<EvChRecv>().connect<&ChannelHandlers::OnRecv>(&sinks.handlers);
        sinks.wired = true;
    }

    inline void ChannelTickSystem(utils::exec::ShardLocal& L, uint64 now_ns, uint64 dt_ns)
    {
        auto& R = L.world;
        auto& pools = R.ctx().get<EcsHandlePools>();

        auto view = R.view<CompChannel, CompEndpoint>();
        for (auto e : view)
        {
            auto& cc = view.get<CompChannel>(e);
            auto& ep = view.get<CompEndpoint>(e);
            auto* st = pools.channels.get(cc.hStore);
            if (!st) continue;

            for (uint32 i = 0; i < CHANNEL_COUNT; ++i)
            {
                if (!CHANNEL_CONFIGS[i].ordered) continue;
                auto& buf = st->buffers[i];
                if (buf.empty()) continue;

                // 타임아웃 검사(가장 오래된 도착 시간 사용)
                auto oldestIt = buf.begin();
                uint64 arrival_ns = oldestIt->second.arrival_ns;
                uint64 elapsed = now_ns - arrival_ns;

                if (elapsed >= ChannelStore::ORDERED_BUFFER_TIMEOUT_NS)
                {
                    // 강제 전진 처리
                    uint64 processed = 0;
                    const uint64 MAX_PROCESS_ON_TIMEOUT = 10;
                    auto& expected = cc.expectedSeqs[i];

                    while (!buf.empty() && processed < MAX_PROCESS_ON_TIMEOUT)
                    {
                        auto next = buf.begin();
                        uint16 seq = next->first;

                        // gap 너무 크면 expected 이동(건너뛰기)
                        if (static_cast<int16>(seq - expected) > ChannelStore::MAX_SEQUENCE_GAP)
                        {
                            expected = static_cast<uint16>(seq + 1);
                        }

                        auto& pkt = next->second;
                        ep.owner->ProcessBufferedPacket(pkt.analysis, const_cast<BYTE*>(pkt.payload.data()), static_cast<uint32>(pkt.payload.size()));
                        buf.erase(next);
                        ++processed;

                        if (seq >= expected)
                            expected = static_cast<uint16>(seq + 1);
                    }
                }
            }
        }
    }
}
