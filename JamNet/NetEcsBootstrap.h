#pragma once
#include "EcsHandle.h"

namespace jam::net::ecs
{
    // Comp::*(EcsHandle) 과 EcsHandlePools::* (EcsHandlePool<Store>)를 템플릿 인자로 받음
    template<typename Comp, EcsHandle Comp::* HandleMember, typename StoreT, EcsHandlePool<StoreT> EcsHandlePools::* PoolMember>
    static void OnConstruct_Handle(entt::registry& R, entt::entity e)
	{
        auto& pools = R.ctx().get<EcsHandlePools>();
        auto& comp = R.get<Comp>(e);
        auto& pool = pools.*PoolMember;
        if (!(comp.*HandleMember).valid()) 
        {
            (comp.*HandleMember) = pool.alloc();
        }
    }

    template<typename Comp, EcsHandle Comp::* HandleMember, typename StoreT, EcsHandlePool<StoreT> EcsHandlePools::* PoolMember>
    static void OnDestroy_Handle(entt::registry& R, entt::entity e)
	{
        auto& pools = R.ctx().get<EcsHandlePools>();
        if (auto* comp = R.template try_get<Comp>(e)) 
        {
            auto& pool = pools.*PoolMember;
            auto& h = comp->*HandleMember;
            if (h.valid()) 
            {
                pool.free(h);
                h = EcsHandle::invalid();
            }
        }
    }




	inline void InstallLifeObserver(entt::registry& R);
	inline void RegisterNetEcs(utils::exec::ShardLocal& L, Service* svc);
}
