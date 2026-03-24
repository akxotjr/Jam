#include "pch.h"
#include "jamnet/sync/replication/ClientSamplingSystem.h"

#include "jamnet/sync/networld/ClientNetWorld.h"
#include "jamnet/sync/replication/NetActorComponents.h"
#include "jamnet/sync/replication/ReplicationEvents.h"
#include "jamnet/sync/replication/NetWorldContext.h"

namespace jam::net
{
	void ClientSamplingSystem::Init()
	{
	}

	void ClientSamplingSystem::Tick()
	{
		uint32 currentTick = m_world.ctx().get<TickCounter>().tick;

		RenderSamplesEvent samples{};
		samples.tick	  = currentTick;
		samples.timestamp = NOW_NS();

		if (auto* netWorld = m_world.ctx().find<ClientNetWorld*>())
		{
			samples.userId = (*netWorld)->GetUserId();
		}

		auto view = m_world.view<NetId, NetActorBodyType, PhysicsSpawnedTag, RemoteActorTag>();
		samples.actors.reserve(view.size_hint() + 1);

		// local
		{
			const entt::entity local = GetLocalEntity(m_world);
			const auto& cs = m_world.ctx().get<LivePredictedState>();

			RenderSamplesEvent::ActorSample sample{
				.objectId = MakeObjectId(local),
				.isLocal  = true,
				.cs		  = cs
			};

			samples.actors.push_back(sample);
		}

		// remote
		for (const auto e : view)
		{
			const auto& bodyType = view.get<NetActorBodyType>(e).body;

			RenderSamplesEvent::ActorSample sample{};

			if (bodyType == px::eBodyType::Character)
			{
				const auto& [cs] = m_world.get<CharAuthorityState>(e);
				sample.objectId	= MakeObjectId(e);
				sample.isLocal	= false;
				sample.cs		= cs;
			}
			else
			{
				const auto& [rs] = m_world.get<RigidAuthorityState>(e);
				sample.objectId	= MakeObjectId(e);
				sample.isLocal	= false;
				sample.rs		= rs;
			}

			samples.actors.push_back(sample);
		}

		if (!samples.actors.empty())
		{
			GLOBAL_EVENTBUS_PUBLISH(std::move(samples));
		}
	}
}
