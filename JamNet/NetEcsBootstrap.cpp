#include "pch.h"
#include "NetEcsBootstrap.h"

#include "Systems.h"

namespace jam::net::ecs
{
    void InstallLifeObserver(entt::registry& R)
    {
        struct LifeObsInstalled {};
        if (!R.ctx().contains<EcsHandlePools>()) R.ctx().emplace<EcsHandlePools>();
        if (R.ctx().contains<LifeObsInstalled>()) return;

        // CompReliable::hStore  <->  EcsHandlePools::reliable
        R.on_construct<ReliabilityState>()
            .connect<&OnConstruct_Handle<ReliabilityState, &ReliabilityState::hStore, ReliabilityStore, &EcsHandlePools::reliability>>();
        R.on_destroy<ReliabilityState>()
            .connect<&OnDestroy_Handle<ReliabilityState, &ReliabilityState::hStore, ReliabilityStore, &EcsHandlePools::reliability>>();

        // CompFragment::hStore  <-> EcsHandlePools::fragments
        // R.on_construct<CompFragment>().connect<&OnConstruct_Handle<CompFragment, &CompFragment::hStore,
        //                                  FragmentStore, &EcsHandlePools::fragments>>();
        // R.on_destroy<CompFragment>().connect<&OnDestroy_Handle<CompFragment, &CompFragment::hStore,
        //                                  FragmentStore, &EcsHandlePools::fragments>>();

        R.ctx().emplace<LifeObsInstalled>();
    }

    void RegisterNetEcs(utils::exec::ShardLocal& L, Service* svc)
	{
		L.world.ctx().emplace<Service*>(svc);
        L.world.ctx().emplace<EcsHandlePools>();

        //L.systems.push_back(&HandshakeSystem);
        L.systems.push_back(&ReliabilityWiringSystem);
        L.systems.push_back(&ReliabilityTickSystem);
        //L.systems.push_back(&NetStatSystem);
        //L.systems.push_back(&FragmentSystem);
        //L.systems.push_back(&CongestionSystem);
        //L.systems.push_back(&EgressSystem);
        //L.systems.push_back(&GroupHomeSystem);


        InstallLifeObserver(L.world);              
	}
}
