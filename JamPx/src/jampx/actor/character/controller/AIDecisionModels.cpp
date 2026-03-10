#include "pch.h"
#include "jampx/actor/character/controller/AIDecisionModels.h"


namespace jam::px
{
	IdleDecisionModel::IdleDecisionModel(const IdleDecisionConfig& cfg)
		: m_cfg(cfg)
	{
	}

	eAIDecisionStatus IdleDecisionModel::Evaluate(const AIContext& ctx, OUT AIDesiredAction& desired)
	{

		desired = {};
		desired.stopMovement = true;

		if (m_cfg.faceTargetIfExists && ctx.hasTarget)
		{
			Vec3 lookDir = ctx.targetPos - ctx.selfPos;
			lookDir.y = 0.f;

			desired.facingYaw = (lookDir.MagnitudeSquared() > EPSILON) ? std::atan2(lookDir.x, lookDir.z) : ctx.selfYaw;
		}
		else if (m_cfg.keepCurrentForwardIfNoTarget)
		{
			desired.facingYaw = ctx.selfYaw;
		}

		return eAIDecisionStatus::Running;
	}




	ChaseDecisionModel::ChaseDecisionModel(const ChaseDecisionConfig& cfg)
		: m_cfg(cfg)
	{
	}

	eAIDecisionStatus ChaseDecisionModel::Evaluate(const AIContext& ctx, AIDesiredAction& desired)
	{
		desired = {};

		if (!ctx.hasTarget)
			return eAIDecisionStatus::Failed;

		if (m_cfg.requireLOS && !ctx.hasLOS)
			return eAIDecisionStatus::Failed;

		const Vec3  toTarget = ctx.targetPos - ctx.selfPos;
		const float dist     = toTarget.Magnitude();

		if (m_cfg.faceTarget)
		{
			Vec3 lookDir = ctx.targetPos - ctx.selfPos;
			lookDir.y = 0.f;

			desired.facingYaw = (lookDir.MagnitudeSquared() > EPSILON) ? std::atan2(lookDir.x, lookDir.z) : ctx.selfYaw;
		}

		if (dist <= m_cfg.stopDistance)
		{
			desired.stopMovement = true;
			return eAIDecisionStatus::Success;
		}

		desired.wantsMove = true;
		desired.moveDir   = toTarget.GetNormalized();
		desired.gait	  = (dist <= m_cfg.slowDistance) ? eGait::Walk : eGait::Run;

		if (m_cfg.enableDash && ctx.canDash && dist >= m_cfg.dashMinDistance)
			desired.dash = true;

		return eAIDecisionStatus::Running;
	}


	PatrolDecisionModel::PatrolDecisionModel(const PatrolDecisionConfig& cfg)
		: m_cfg(cfg)
	{
	}

	void PatrolDecisionModel::Reset()
	{
		IAIDecisionModel::Reset();
		m_currentIdx = 0;
		m_pingPongDir = 1;
	}

	eAIDecisionStatus PatrolDecisionModel::Evaluate(const AIContext& ctx, AIDesiredAction& desired)
	{
		desired = {};

		if (!ctx.hasPath || ctx.pathPoints.empty())
			return eAIDecisionStatus::Failed;

		const auto& points = ctx.pathPoints;
		const int32 count = static_cast<int32>(points.size());

		m_currentIdx = std::clamp(m_currentIdx, 0, count - 1);

		const Vec3  toPoint = points[m_currentIdx] - ctx.selfPos;
		const float dist    = toPoint.Magnitude();

		// 도착 판정 → 내부적으로 다음 waypoint 진행
		if (dist <= m_cfg.arriveDistance)
		{
			if (m_cfg.mode == PatrolMode::Loop)
			{
				m_currentIdx = (m_currentIdx + 1) % count;
			}
			else // PingPong
			{
				m_currentIdx += m_pingPongDir;

				if (m_currentIdx >= count)
				{
					m_currentIdx  = count - 2;
					m_pingPongDir = -1;
				}
				else if (m_currentIdx < 0)
				{
					m_currentIdx  = 1;
					m_pingPongDir = 1;
				}
			}
			m_currentIdx = std::clamp(m_currentIdx, 0, count - 1);
		}

		desired.wantsMove = true;
		desired.moveDir   = toPoint.GetNormalized();

		if (m_cfg.faceMoveDirection)
			desired.facingYaw = std::atan2(desired.moveDir.x, desired.moveDir.z);

		return eAIDecisionStatus::Running;
	}




	// ----------------------------------------------------------------
	// PriorityDecisionModel
	// ----------------------------------------------------------------

	void PriorityDecisionModel::Add(std::unique_ptr<IAIDecisionModel> model)
	{
		m_models.push_back(std::move(model));
	}

	void PriorityDecisionModel::Reset()
	{
		for (auto& m : m_models)
			m->Reset();
	}

	eAIDecisionStatus PriorityDecisionModel::Evaluate(const AIContext& ctx, AIDesiredAction& desired)
	{
		for (auto& model : m_models)
		{
			AIDesiredAction candidate{};
			const eAIDecisionStatus status = model->Evaluate(ctx, candidate);

			if (status != eAIDecisionStatus::Failed)
			{
				desired = candidate;
				return status;
			}
		}

		return eAIDecisionStatus::Failed;
	}


} // namespace jam::px
