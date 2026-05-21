#include "pch.h"
#include "jamnet/sync/replication/ClientSamplingSystem.h"

#include "jamnet/sync/networld/ClientPhysicalWorld.h"
#include "jamnet/sync/replication/NetActorComponents.h"
#include "jamnet/runtime/AppRuntimeEvents.h"
#include "jamnet/sync/replication/WorldContext.h"


namespace jam::net
{
	namespace
	{
		// Keep some visual prediction lead for responsiveness, but compress excess lead
		// so click-move reconciliation does not visibly snap backward and forward.
		px::Vec3 CompressVisualLead(const px::Vec3& authPos, const px::Vec3& sampledPos)
		{
			px::Vec3 horizontalLead = sampledPos - authPos;
			horizontalLead.y = 0.0f;

			const float leadDist = horizontalLead.Magnitude();
			static constexpr float kLeadSoftCap      = 0.75f;
			static constexpr float kLeadCompression  = 0.35f;
			static constexpr float kLeadHardCap      = 1.25f;

			if (leadDist <= kLeadSoftCap)
				return sampledPos;

			px::Vec3 compressedPos = sampledPos;
			const px::Vec3 leadDir = horizontalLead.GetNormalized();

			float compressedLead = kLeadSoftCap + ((leadDist - kLeadSoftCap) * kLeadCompression);
			compressedLead = std::min(compressedLead, kLeadHardCap);

			compressedPos.x = authPos.x + (leadDir.x * compressedLead);
			compressedPos.z = authPos.z + (leadDir.z * compressedLead);
			return compressedPos;
		}
	}

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

		if (auto* physicalWorld = m_world.ctx().find<ClientPhysicalWorld*>())
		{
			snapshotEvent.accountId = (*physicalWorld)->GetAccountId();
			snapshotEvent.userId = (*physicalWorld)->GetUserId();
			snapshotEvent.worldId = (*physicalWorld)->GetWorldKey().worldId;
		}

		auto view = m_world.view<NetId, NetActorBodyType, PhysicsSpawnedTag, RemoteActorTag>();
		snapshotEvent.frame.actors.reserve(view.size_hint() + 1);

		// local
		{
			const entt::entity local = GetCachedLocalEntity(m_world);

			const auto& live = m_world.ctx().get<LivePredictedState>();
			auto& delta = m_world.ctx().get<RenderCorrectionDelta>();

			px::CharacterState sampled = live;

			// correction commit 직후 visual jump 완화를 위해 렌더 보정 오프셋을 반영한다.
			sampled.pos			= sampled.pos - delta.pos;
			sampled.facingYaw   = sampled.facingYaw - delta.yaw;
			sampled.facingPitch = sampled.facingPitch - delta.pitch;

			if (local != entt::null && m_world.valid(local) && m_world.all_of<CharAuthorityState>(local))
			{
				const auto& auth = m_world.get<CharAuthorityState>(local).state;
				sampled.pos = CompressVisualLead(auth.pos, sampled.pos);
			}

			ActorPresentationState actorSnapshot{
				.objectId = MakeObjectId(local),
				.isLocal = true,
				.cs = sampled,
				.csRaw = live
			};
			if (local != entt::null && m_world.valid(local) && m_world.all_of<NetId>(local))
				actorSnapshot.netId = m_world.get<NetId>(local);

			snapshotEvent.frame.actors.push_back(std::move(actorSnapshot));

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
			const NetId netId = view.get<NetId>(e);

			if (bodyType == px::eBodyType::Character)
			{
				const auto& [cs] = m_world.get<CharAuthorityState>(e);
				snapshotEvent.frame.actors.push_back(ActorPresentationState{
					.objectId = MakeObjectId(e),
					.netId = netId,
					.isLocal = false,
					.cs = cs
				});

			}
			else
			{
				const auto& [rs] = m_world.get<RigidAuthorityState>(e);
				snapshotEvent.frame.actors.push_back(ActorPresentationState{
					.objectId = MakeObjectId(e),
					.netId = netId,
					.isLocal = false,
					.rs = rs
				});

			}
		}

		GLOBAL_EVENTBUS_PUBLISH(snapshotEvent);
	}
}
