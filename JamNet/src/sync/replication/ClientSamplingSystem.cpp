#include "pch.h"
#include "jamnet/sync/replication/ClientSamplingSystem.h"

#include "jamnet/sync/networld/ClientNetWorld.h"
#include "jamnet/sync/replication/NetActorComponents.h"
#include "jamnet/sync/replication/ReplicationEvents.h"
#include "jamnet/sync/replication/NetWorldContext.h"
#include <cmath>

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

			const auto& live = m_world.ctx().get<LivePredictedState>();
			auto& delta = m_world.ctx().get<RenderCorrectionDelta>();

			px::CharacterState sampled = live;

			// correction commit 직후 visual jump 완화를 위해 렌더 보정 오프셋을 반영한다.
			sampled.pos = sampled.pos - delta.pos;
			sampled.facingYaw = sampled.facingYaw - delta.yaw;
			sampled.facingPitch = sampled.facingPitch - delta.pitch;

			RenderSamplesEvent::ActorSample sample{
				.objectId = MakeObjectId(local),
				.isLocal  = true,
				.cs		  = sampled,
				.csRaw	  = live
			};

			samples.actors.push_back(sample);

			// sampling 단계에서 render correction delta를 점진적으로 0으로 수렴시킨다.
			static constexpr float kDeltaDecay = 0.80f;
			static constexpr float kPosEpsSq   = 0.000001f;
			static constexpr float kAngEps     = 0.0001f;

			delta.pos   = delta.pos * kDeltaDecay;
			delta.yaw   = delta.yaw * kDeltaDecay;
			delta.pitch = delta.pitch * kDeltaDecay;

			if (delta.pos.MagnitudeSquared() <= kPosEpsSq)
				delta.pos = px::Vec3::Zero();
			if (std::abs(delta.yaw) <= kAngEps)
				delta.yaw = 0.0f;
			if (std::abs(delta.pitch) <= kAngEps)
				delta.pitch = 0.0f;
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
