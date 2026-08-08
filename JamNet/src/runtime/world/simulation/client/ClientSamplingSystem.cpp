#include "pch.h"
#include "jamnet/runtime/world/simulation/client/ClientSamplingSystem.h"

#include "jamnet/runtime/world/simulation/client/ClientWorld.h"
#include "jamnet/runtime/world/simulation/client/ClientPhysicsSystem.h"
#include "jamnet/runtime/world/simulation/common/ActorComponents.h"
#include "jamnet/runtime/world/simulation/common/WorldContext.h"
#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/core/executor/ShardExecutor.h"

#include "jamnet/runtime//application/AppRuntimeEvents.h"


namespace jam::net
{
	void ClientSamplingSystem::Init()
	{
	}

	void ClientSamplingSystem::Tick()
	{
		uint32 currentTick = m_world.ctx().get<TickCounter>().tick;

		PresentationFramePushedEvent snapshotEvent{};
		snapshotEvent.frame.tick = currentTick;
		snapshotEvent.frame.timestamp = static_cast<float>(NOW_NS());
		snapshotEvent.frame.sequence = currentTick;

		if (auto* physicalWorld = m_world.ctx().find<ClientWorld*>())
		{
			snapshotEvent.accountId = (*physicalWorld)->GetAccountId();
			snapshotEvent.userId	= (*physicalWorld)->GetUserId();
			snapshotEvent.worldId	= (*physicalWorld)->GetWorldId();
		}

		auto view = m_world.view<ActorId, ActorBodyType, PhysicsSpawnedTag, RemoteActorTag>();
		snapshotEvent.frame.actors.reserve(view.size_hint() + 1);

		// local
		{
			const entt::entity local = GetCachedLocalEntity(m_world);
			auto& delta = m_world.ctx().get<RenderCorrectionDelta>();

			if (local != entt::null
				&& m_world.valid(local)
				&& m_world.all_of<ActorId, CharAuthorityState>(local))
			{
				const auto& live = m_world.ctx().get<LivePredictedState>();
				px::CharacterState sampled = live;

				// correction commit 직후 visual jump 완화를 위해 렌더 보정 오프셋을 반영한다.
				sampled.pos	= sampled.pos - delta.pos;

				ActorPresentationState actorSnapshot{
					.actorId = GetActorIdComponent(m_world, local),
					.isLocal = true,
					.cs		 = sampled,
				};

				snapshotEvent.frame.actors.push_back(std::move(actorSnapshot));
			}

			// sampling 단계에서 render correction delta를 점진적으로 0으로 수렴시킨다.
			static constexpr float kDeltaDecay = 0.80f;
			static constexpr float kPosEpsSq   = 0.000001f;

			delta.pos = delta.pos * kDeltaDecay;

			if (delta.pos.MagnitudeSquared() <= kPosEpsSq)
				delta.pos = px::Vec3::Zero();
		}

		// remote
		for (const auto e : view)
		{
			const auto& bodyType = view.get<ActorBodyType>(e).body;

			if (bodyType == px::eBodyType::Character)
			{
				const auto& [cs] = m_world.get<CharAuthorityState>(e);
				snapshotEvent.frame.actors.push_back(ActorPresentationState{
					.actorId = GetActorIdComponent(m_world, e),
					.isLocal = false,
					.cs = cs
				});

			}
			else
			{
				const auto& [rs] = m_world.get<RigidAuthorityState>(e);
				snapshotEvent.frame.actors.push_back(ActorPresentationState{
					.actorId = GetActorIdComponent(m_world, e),
					.isLocal = false,
					.rs = rs
				});

			}
		}

		GLOBAL_EVENTBUS_PUBLISH(snapshotEvent);
	}
}
