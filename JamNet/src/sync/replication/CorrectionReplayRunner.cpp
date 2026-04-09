#include "pch.h"
#include "jamnet/sync/replication/CorrectionReplayRunner.h"

#include <jampx/IPhysicsFacade.h>

#include "jamnet/sync/replication/NetWorldContext.h"

namespace jam::net
{
	CorrectionReplayRunner::CorrectionReplayRunner(px::IPhysicsFacade* physics)
		: m_physics(physics)
	{
	}

	void CorrectionReplayRunner::Prepare(entt::registry& world, const ReplayContext& ctx)
	{
		m_relevantEntities.clear();
		if (ctx.local == entt::null || !world.valid(ctx.local))
			return;

		px::ActorContext localCtx{};
		localCtx.oid   = MakeObjectId(ctx.local);
		localCtx.state = world.get<CharAuthorityState>(ctx.local).state;

		std::vector<px::ActorContext> one;
		one.reserve(1);
		one.push_back(localCtx);

		m_physics->PushReplayStates(one);

		auto view = world.view<ReplayRelevantTag, NetActorBodyType>();
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
			if (!world.valid(e) || !world.all_of<NetActorBodyType>(e))
				continue;

			px::ActorContext ac{};
			ac.oid = MakeObjectId(e);

			const auto body = world.get<NetActorBodyType>(e).body;
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
			delta = {};
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
			//correction = live;  // replay 샘플이 없으면 correction을 기존 live로 유지

			// replay 샘플이 없으면(local ack==current 등) authoritative를 기준으로 correction 구성
			if (ctx.local != entt::null && world.valid(ctx.local) && world.all_of<CharAuthorityState>(ctx.local))
				correction = world.get<CharAuthorityState>(ctx.local).state;
			else
				correction = live;
		}

		if (replayStats.meaningfulInputCount == 0)
		{
			delta = {};
		}
		else
		{
			delta.pos   = correction.pos - preLive.pos;
			delta.yaw   = correction.facingYaw - preLive.facingYaw;
			delta.pitch = correction.facingPitch - preLive.facingPitch;
		}

		live = correction; // logical state overwrite

		replayBuf.Clear();
	}

} // namespace jam::net

