#include "pch.h"
#include "jampx/character/AIControllerComponent.h"


namespace jam::px
{
	MoveIntent AIControllerComponent::BuildIntent(float dt)
	{
		if (!m_model) return {};

		AIDesiredAction action{};

		eAIDecisionStatus status = m_model->Evaluate(m_ctx, action);

		MoveIntent intent{};
		intent.moveYaw			= m_ctx.selfYaw;
		intent.gaitRequest		= action.gait;
		intent.stanceRequest	= action.stance;
		intent.jumpPressed		= action.jump;
		intent.jumpHeld			= action.jump;
		intent.dashPressed		= action.dash;

		if (action.wantsMove && action.moveDir.MagnitudeSquared() > EPSILON)
		{
			// world-space dir 직접 전달 → BuildWishMovement 에서 변환 없이 사용
			intent.wishDir = action.moveDir.GetNormalized();
			intent.moveMag = 1.0f;
		}

		return intent;
	}
} // namespace jam::px