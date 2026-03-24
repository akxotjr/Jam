#include "pch.h"
#include "jamnet/sync/replication/ClientReplaySystem.h"
#include "jamnet/sync/replication/NetActorComponents.h"


namespace jam::net
{
	ClientReplaySystem::ClientReplaySystem(entt::registry& world)
		: m_world(world)
	{
	}

	void ClientReplaySystem::Init(const ReplayRelevanceConfig& cfg)
	{
		m_cfg = cfg;
	}

	void ClientReplaySystem::Tick()
	{
		const entt::entity local = GetLocalEntity(m_world);
		if (local == entt::null || !m_world.valid(local) || !m_world.all_of<CharProxyState>(local))
			return;

		const px::Vec3 localPos = m_world.get<CharProxyState>(local).state.pos;

		UpdateCharacterCandidates(local, localPos);
		UpdateRigidCandidates(localPos);
	}

	void ClientReplaySystem::UpdateCharacterCandidates(entt::entity local, const px::Vec3& localPos)
	{
		auto view = m_world.view<ReplayCandidateTag, ReplayRetention, CharProxyState>();
		for (auto e : view)
		{
			if (e == local) continue;

			auto& retention = view.get<ReplayRetention>(e);
			const auto& ps  = view.get<CharProxyState>(e).state;

			const bool wasRelevant = m_world.all_of<ReplayRelevantTag>(e);

			const float radius   = wasRelevant ? m_cfg.leaveRadius : m_cfg.enterRadius;
			const float radiusSq = radius * radius;
			const float distSq   = (ps.pos - localPos).MagnitudeSquared();

			if (distSq <= radiusSq)
			{
				retention.minHoldTicks   = m_cfg.minHoldTicks;
				retention.remainingTicks = m_cfg.minHoldTicks;

				if (!wasRelevant)
					m_world.emplace<ReplayRelevantTag>(e);
			}
			else if (wasRelevant)
			{
				if (retention.remainingTicks > 0)
				{
					--retention.remainingTicks;
				}
				else
				{
					m_world.remove<ReplayRelevantTag>(e);
				}
			}
		}
	}

	void ClientReplaySystem::UpdateRigidCandidates(const px::Vec3& localPos)
	{
		auto view = m_world.view<ReplayCandidateTag, ReplayRetention, RigidProxyState>();
		for (auto e : view)
		{
			auto& retention = view.get<ReplayRetention>(e);
			const auto& ps = view.get<RigidProxyState>(e).state;

			const bool wasRelevant = m_world.all_of<ReplayRelevantTag>(e);

			const float radius   = wasRelevant ? m_cfg.leaveRadius : m_cfg.enterRadius;
			const float radiusSq = radius * radius;
			const float distSq   = (ps.pose.p - localPos).MagnitudeSquared();

			if (distSq <= radiusSq)
			{
				retention.minHoldTicks   = m_cfg.minHoldTicks;
				retention.remainingTicks = m_cfg.minHoldTicks;

				if (!wasRelevant)
					m_world.emplace<ReplayRelevantTag>(e);
			}
			else if (wasRelevant)
			{
				if (retention.remainingTicks > 0)
				{
					--retention.remainingTicks;
				}
				else
				{
					m_world.remove<ReplayRelevantTag>(e);
				}
			}
		}
	}

} // namespace jam::net
