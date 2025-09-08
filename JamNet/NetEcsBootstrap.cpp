#include "pch.h"
#include "NetEcsBootstrap.h"

#include "EcsReliability.hpp"
#include "EcsChannel.hpp"
#include "EcsCongestionControl.hpp"
#include "EcsFragment.hpp"
#include "EcsHandshake.hpp"
#include "EcsNetstat.hpp"

namespace jam::net::ecs
{
    void InstallLifeObserver(entt::registry& R)
    {
        struct LifeObsInstalled {};
        if (!R.ctx().contains<EcsHandlePools>()) R.ctx().emplace<EcsHandlePools>();
        if (R.ctx().contains<LifeObsInstalled>()) return;

        // CompReliable::hStore  <->  EcsHandlePools::reliable
        R.on_construct<CompReliability>()
            .connect<&OnConstruct_Handle<CompReliability, &CompReliability::hStore, ReliabilityStore, &EcsHandlePools::reliability>>();
        R.on_destroy<CompReliability>()
            .connect<&OnDestroy_Handle<CompReliability, &CompReliability::hStore, ReliabilityStore, &EcsHandlePools::reliability>>();

        // CompFragment::hStore  <-> EcsHandlePools::fragments
        R.on_construct<CompFragment>()
    		.connect<&OnConstruct_Handle<CompFragment, &CompFragment::hStore, FragmentStore, &EcsHandlePools::fragments>>();
        R.on_destroy<CompFragment>()
    		.connect<&OnDestroy_Handle<CompFragment, &CompFragment::hStore, FragmentStore, &EcsHandlePools::fragments>>();

        // CompChannel::hStore <-> EcsHandlePools::channel
        R.on_construct<CompChannel>()
    		.connect<&OnConstruct_Handle<CompChannel, &CompChannel::hStore, ChannelStore, &EcsHandlePools::channel>>();
        R.on_destroy<CompChannel>()
            .connect<&OnDestroy_Handle<CompChannel, &CompChannel::hStore, ChannelStore, &EcsHandlePools::channel>>();

        R.ctx().emplace<LifeObsInstalled>();
    }

    void RegisterNetEcs(utils::exec::ShardLocal& L, Service* svc)
	{
		L.world.ctx().emplace<Service*>(svc);

        L.systems.push_back(&HandshakeWiringSystem);
        L.systems.push_back(&ChannelWiringSystem);
        L.systems.push_back(&FragmentWiringSystem);
        L.systems.push_back(&NetstatWiringSystem);
        L.systems.push_back(&CongestionControlWiringSystem);

        L.systems.push_back(&HandshakeTickSystem);
        L.systems.push_back(&ChannelTickSystem);
        L.systems.push_back(&NetstatTickSystem);

        L.world.ctx().emplace<EcsHandlePools>();
        InstallLifeObserver(L.world);              
	}
}
