#include "pch.h"
#include "jamnet/runtime/world/simulation/common/CorrectionReplayRunner.h"
#include "jamnet/runtime/world/simulation/common/WorldContext.h"

#include <jampx/PhysicsFacade.h>

namespace jam::net
{
	CorrectionReplayRunner::CorrectionReplayRunner(px::PhysicsFacade* physics)
		: m_physics(physics)
	{
	}

	void CorrectionReplayRunner::Prepare(entt::registry& world, const ReplayContext& ctx)
	{
		m_relevantEntities.clear();
		if (ctx.local == entt::null || !world.valid(ctx.local))
			return;

		px::ActorContext localCtx{};
		localCtx.actorId   = GetPhysicsActorId(world, ctx.local);
		localCtx.state = world.get<CharAuthorityState>(ctx.local).state;

		std::vector<px::ActorContext> one;
		one.reserve(1);
		one.push_back(localCtx);

		m_physics->PushReplayStates(one);

		auto view = world.view<ReplayRelevantTag, ActorBodyType>();
		for (auto e : view)
		{
			if (e == ctx.local) continue;
			m_relevantEntities.push_back(e);
		}
	}

	void CorrectionReplayRunner::Run(entt::registry& world, const ReplayContext& ctx)
	{
		std::vector<px::ActorContext> perTick;
		perTick.reserve(m_relevantEntities.size());

		for (const entt::entity e : m_relevantEntities)
		{
			if (!world.valid(e) || !world.all_of<ActorBodyType>(e))
				continue;

			px::ActorContext ac{};
			ac.actorId = GetPhysicsActorId(world, e);

			const auto body = world.get<ActorBodyType>(e).body;
			if (body == px::eBodyType::Character)
			{
				if (world.all_of<CharReplayHistory>(e))
				{
					const auto& h = world.get<CharReplayHistory>(e);
					if (const auto* s = h.FindLatestLE(ctx.tick)) ac.state = s->state;
					else ac.state = world.get<CharAuthorityState>(e).state;
				}
				else ac.state = world.get<CharAuthorityState>(e).state;
			}
			else
			{
				if (world.all_of<RigidReplayHistory>(e))
				{
					const auto& h = world.get<RigidReplayHistory>(e);
					if (const auto* s = h.FindLatestLE(ctx.tick)) ac.state = s->state;
					else ac.state = world.get<RigidAuthorityState>(e).state;
				}
				else ac.state = world.get<RigidAuthorityState>(e).state;
			}

			perTick.push_back(ac);
		}

		if (!perTick.empty())
			m_physics->PushReplayStates(perTick);
	}

	void CorrectionReplayRunner::Commit(entt::registry& world, const ReplayContext& ctx)
	{
		auto& replayBuf   = world.ctx().get<ReplayPredictedBuffer>();
		auto& predHist    = world.ctx().get<PredictedHistoryBuffer>();
		auto& replayStats = world.ctx().get<ReplayStats>();
		auto& live		  = world.ctx().get<LivePredictedState>();
		auto& correction  = world.ctx().get<CorrectionState>();
		auto& delta		  = world.ctx().get<RenderCorrectionDelta>();

		const px::CharacterState preLive = live;

		if (replayStats.truncated)
		{
			correction = live;
			replayBuf.Clear();
			return;
		}

		predHist.PruneFrom(ctx.inputAck + 1);

		for (const auto& s : replayBuf.samples)
		{
			predHist.Push(s.inputSeq, s.state);
		}

		if (!replayBuf.samples.empty())
		{
			correction = replayBuf.samples.back().state;
		}
		else
		{
			if (ctx.local != entt::null && world.valid(ctx.local) && world.all_of<CharAuthorityState>(ctx.local))
				correction = world.get<CharAuthorityState>(ctx.local).state;
			else
				correction = live;
		}

		// Local orientation is derived from the latest local control profile and
		// does not affect capsule movement. Position replay must not replace it
		// with an older authoritative orientation.
		correction.bodyYaw = preLive.bodyYaw;
		correction.viewYaw = preLive.viewYaw;
		correction.viewPitch = preLive.viewPitch;

		if (!m_physics
			|| ctx.local == entt::null
			|| !world.valid(ctx.local)
			|| !m_physics->SetCharacterState(GetPhysicsActorId(world, ctx.local), correction))
		{
			correction = preLive;
			replayBuf.Clear();
			return;
		}

		// Preserve the visual pose that was already being presented. A new logical
		// correction is accumulated on top of any remaining render-space offset,
		// then ClientSamplingSystem continues decaying it toward zero.
		delta.pos = delta.pos + (correction.pos - preLive.pos);

		live = correction; // logical state overwrite

		replayBuf.Clear();
	}

} // namespace jam::net
