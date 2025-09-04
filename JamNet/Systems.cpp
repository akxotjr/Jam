#include "pch.h"
#include "Systems.h"
#include "Components.h"
#include "EcsEvents.h"
#include "NetStatManager.h"
#include "ReliableTransportManager.h"

namespace jam::net::ecs
{

#pragma region Reliability System

    void ReliabilityWiringSystem(utils::exec::ShardLocal& L, uint64, uint64)
    {
        auto& R = L.world;
        auto& D = L.events;

        auto& sinks = R.ctx().emplace<ReliabilitySinks>();
        auto& handlers = R.ctx().emplace<ReliabilityHandlers>();

        if (!sinks.wired)
        {
            sinks.onAck = D.sink<EvRecvAck>().connect<&ReliabilityHandlers::OnRecvAck>(&handlers);


            sinks.wired = true;
        }
    }

    void ReliabilityTickSystem(utils::exec::ShardLocal& L, uint64 now_ns, uint64 dt_ns)
    {
        auto& R = L.world;
        auto& D = L.events;

        auto view = R.view<ReliabilityState, SessionRef>();
        for (auto e : view) 
        {
            auto& rs = view.get<ReliabilityState>(e);
            auto& session = view.get<SessionRef>(e);

            auto& pools = R.ctx().get<EcsHandlePools>();
            auto* store = pools.reliability.get(rs.hStore);
            if (!store) continue;


        }
    }



#pragma endregion

}
