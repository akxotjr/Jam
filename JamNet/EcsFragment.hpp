#pragma once

#include "pch.h"

#include "EcsHandle.h"
#include "EcsReliability.hpp"
#include "FragmentManager.h"
#include "PacketBuilder.h"

namespace jam::net::ecs
{
	// Components

	struct CompFragment
	{
		EcsHandle                           hStore = EcsHandle::invalid();
	};

	struct FragmentStore
	{
		xumap<uint16, FragmentReassembly>   reassemblies;
	};

	// Events

	struct EvFgFragmentize
	{
        entt::entity        e{ entt::null };
        Sptr<SendBuffer>    buf;
        PacketAnalysis      analysis;
	};

	struct EvFgReassemble
	{
		entt::entity        e{ entt::null };
		BYTE*               buf;
		uint16              size;
	};


	// Handlers

    struct FragmentHandlers
	{
        entt::registry* R{};

        void Fragmentize(const EvFgFragmentize& ev)
        {
            xvector<Sptr<SendBuffer>> fragments;

            if (!ev.buf || ev.analysis.payloadSize == 0) return;

            uint8 totalCount = (size + MAX_FRAGMENT_PAYLOAD_SIZE - 1) / MAX_FRAGMENT_PAYLOAD_SIZE;  //todo: what is size?
            if (totalCount > MAX_FRAGMENTS)
                return;

            PacketHeader originHeader = ev.analysis.header;
            BYTE* payload = ev.analysis.GetPayloadPtr(ev.buf->Buffer());

            const uint16 baseSeq = AllocSeqRange(*R, ev.e, totalCount);

            for (uint8 i = 0; i < totalCount; i++)
            {
                uint32 offset = i * MAX_FRAGMENT_PAYLOAD_SIZE;
                uint32 payloadSize = min(MAX_FRAGMENT_PAYLOAD_SIZE, size - offset);

                auto fragment = PacketBuilder::CreatePacket(
                    U2E(ePacketType, originHeader.GetType()),
                    originHeader.GetId(),
                    originHeader.GetFlags() | PacketFlags::FRAGMENTED,
                    eChannelType::RELIABLE_ORDERED,
                    payload + offset,
                    payloadSize,
                    baseSeq + i,
                    i,
                    totalCount
                );

                fragments.push_back(fragment);
            }

            // todo
            //auto& ep = R->get<CompEndpoint>(ev.e);
            //ep.owner->
        }

    	void Reassemble(const EvFgReassemble& ev)
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

            uint16 groupKey = static_cast<uint16>(seq - fragIdx);
            auto& re = st->reassemblies[groupKey];

            if (!re.headerSaved) 
            {
                re.Init(fragTot);
                re.originalHeader = an.header;
                re.originalHeader.SetFlags(re.originalHeader.GetFlags() & ~PacketFlags::FRAGMENTED);
                re.headerSaved = true;
            }
            if (re.totalCount != fragTot)
            {
	            st->reassemblies.erase(groupKey);
            	return;
            }
            if (re.recvFragments.size() > fragIdx && re.recvFragments[fragIdx])
            {
	            return;
            }
            if (!re.WriteFragment(fragIdx, payload, psize))
            {
                return;
            }

    		re.lastRecvTime = now;

            if (re.IsComplete()) 
            {
                auto data = re.GetReassembledData();
                auto orig = re.originalHeader;
                st->reassemblies.erase(groupKey);
                
				// todo
                auto& ep = R->get<CompEndpoint>(ev.e);
                //ep.owner->
            }
        }
    };

    // Sinks

    struct FragmentSinks
	{
        bool                        wired = false;

    	entt::scoped_connection     onFragmentize;
        entt::scoped_connection     onReassemble;

        FragmentHandlers            handlers;
    };

	// Systems

    inline void FragmentWiringSystem(utils::exec::ShardLocal& L, uint64, uint64)
	{
        auto& R = L.world;
    	auto& D = L.events;
        auto& sinks = R.ctx().emplace<FragmentSinks>();
    	if (sinks.wired) return;
    	sinks.handlers.R = &R;

        sinks.onFragmentize     = D.sink<EvFgFragmentize>().connect<&FragmentHandlers::Fragmentize>(&sinks.handlers);
        sinks.onReassemble      = D.sink<EvFgReassemble>().connect<&FragmentHandlers::Reassemble>(&sinks.handlers);

        sinks.wired = true;
    }
}
