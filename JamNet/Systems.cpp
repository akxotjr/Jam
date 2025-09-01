#include "pch.h"
#include "Systems.h"
#include "Components.h"

namespace jam::net::ecs
{
    void SessionUpdateSystem(utils::exec::ShardLocal& L, uint64 now_ns, uint64 dt_ns)
    {
        auto view = L.world.view<SessionRef>();
        for (auto e : view)
        {
            if (auto sp = view.get<SessionRef>(e).wp.lock())
            {
                sp->UpdateShard(now_ns, dt_ns); // 기존 로직 호출(브릿지)
            }
        }
    }
}
