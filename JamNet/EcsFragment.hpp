#pragma once

#include "pch.h"

#include "EcsHandle.h"
#include "FragmentManager.h"

namespace jam::net::ecs
{
	// Components

	struct CompFragment
	{
		EcsHandle hStore = EcsHandle::invalid();
	};

	struct FragmentStore
	{
		xumap<uint16, FragmentReassembly> reassemblies;
	};

	// Events

	// 수신 원시 버퍼 → 재조립 시도
	struct EvFragRecvRaw
	{
		entt::entity e;
		BYTE* buf;
		uint16 size;
	};
	// 재조립 성공 시 알림 (이벤트 체인용)
	struct EvFragReassembled
	{
		entt::entity e;
		PacketHeader original;
		xvector<BYTE> data;
	};

	// Handlers

    struct FragmentHandlers
	{
        entt::registry* R{};

    	void OnRecvRaw(const EvFragRecvRaw& ev)
    	{
            auto& pools = R->ctx().get<EcsHandlePools>();
            auto& cf = R->get<CompFragment>(ev.e);
            auto* st = pools.fragments.get(cf.hStore);
            if (!st) return;

            PacketAnalysis an = PacketBuilder::AnalyzePacket(ev.buf, ev.size);
            if (!an.isValid || !an.IsFragmented()) return;

            uint8 fragIdx = an.GetFragmentIndex();
            uint8 fragTot = an.GetTotalFragments();
            uint16 seq = an.GetSequence();
            if (fragTot == 0 || fragIdx >= fragTot) return;

            auto payload = an.GetPayloadPtr(ev.buf);
            uint32 psize = an.payloadSize;

            // CleanupStaleReassemblies (lazy)
            uint64 now = utils::Clock::Instance().GetCurrentTick();
            {
                xvector<uint16> stale;
                for (auto& [k, re] : st->reassemblies)
                    if (now - re.lastRecvTime > REASSEMBLY_TIMEOUT_TICK) stale.push_back(k);
                for (auto k : stale) st->reassemblies.erase(k);
            }

            uint16 groupKey = (uint16)(seq - fragIdx);
            auto& re = st->reassemblies[groupKey];
            if (!re.headerSaved) 
            {
                re.Init(fragTot);
                re.originalHeader = an.header;
                re.originalHeader.SetFlags(re.originalHeader.GetFlags() & ~PacketFlags::FRAGMENTED);
                re.headerSaved = true;
            }
            if (re.totalCount != fragTot) { st->reassemblies.erase(groupKey); return; }
            if (re.recvFragments.size() > fragIdx && re.recvFragments[fragIdx]) { return; } // dup

            if (!re.WriteFragment(fragIdx, payload, psize)) return;
            re.lastRecvTime = now;

            if (re.IsComplete()) 
            {
                auto data = re.GetReassembledData();
                auto orig = re.originalHeader;
                st->reassemblies.erase(groupKey);
                // 재조립 완료 이벤트 재발행(샤드 스레드)
                R->ctx().get<entt::dispatcher>().trigger<EvFragReassembled>(EvFragReassembled{ ev.e, orig, std::move(data) });
            }
        }
    };

    // Sinks

    struct FragmentSinks
	{
        bool wired = false;
        entt::scoped_connection onRaw;
        FragmentHandlers handlers;
    };

	// Systems

    inline void FragmentWiringSystem(utils::exec::ShardLocal& L, uint64, uint64)
	{
        auto& R = L.world;
    	auto& D = L.events;
        auto& sinks = R.ctx().emplace<FragmentSinks>();
    	if (sinks.wired) return;
    	sinks.handlers.R = &R;

        sinks.onRaw = D.sink<EvFragRecvRaw>().connect<&FragmentHandlers::OnRecvRaw>(&sinks.handlers);

        sinks.wired = true;
    }
}
