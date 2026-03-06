#include "pch.h"
#include "jamnet/sync/replication/ClientSamplingSystem.h"

#include "jamnet/sync/networld/ClientNetWorld.h"
#include "jamnet/sync/replication/ClientPhysicsSystem.h"
#include "jamnet/sync/replication/NetActorComponents.h"
#include "jamnet/sync/replication/ReplicationEvents.h"
#include "jamnet/sync/replication/ClientReplicationSystem.h"
#include "jamnet/sync/replication/NetWorldContext.h"

namespace jam::net
{
	void ClientSamplingSystem::Init()
	{
		// 초기화 로직 (필요시)
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

		uint32 localNetId = 0;
		if (auto* repl = m_world.ctx().find<ClientReplicationSystem>())
		{
			localNetId = repl->GetLocalNetId();
		}

		px::Vec3 visualOffset = px::Vec3::Zero();
		if (auto* phys = m_world.ctx().find<ClientPhysicsSystem>())
			visualOffset = phys->GetVisualOffset();

		auto TruncateToCm = [](px::Vec3 v) -> px::Vec3
		{
				return { 
					std::round(v.x * 100.f) / 100.f, 
					std::round(v.y * 100.f) / 100.f, 
					std::round(v.z * 100.f) / 100.f 
				};
		};

		auto view = m_world.view<NetIdentity, NetActorBodyKind>();
		samples.actors.reserve(view.size_hint());

		for (auto e : view)
		{
			const auto& identity = view.get<NetIdentity>(e);
			const auto& kind	 = view.get<NetActorBodyKind>(e);

			RenderSamplesEvent::ActorSample sample{};

			if (px::IsCharacterBody(kind.body))
			{
				auto cs = m_world.get<px::CharacterState>(e);

				sample.objectId		= MakeObjectId(e);
				sample.isLocal	= (identity.netId == localNetId);
				sample.cs		= cs;

				cs.pos = TruncateToCm(cs.pos);

				JAMNET_LOG_DEBUG("Sampling system | character | pos({}, {}, {})", cs.pos.x, cs.pos.y, cs.pos.z);
			}
			else
			{
				auto rs = m_world.get<px::RigidState>(e);
				rs.pose.p = TruncateToCm(rs.pose.p);

				sample.objectId		= MakeObjectId(e);
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
