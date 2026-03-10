#include "pch.h"
#include "jampx/character/PlayerControllerComponent.h"

namespace jam::px
{
	MoveIntent PlayerControllerComponent::BuildIntent(float dt)
	{
		MoveIntent intent{};
		intent.moveYaw = m_input.facingYaw;

		float x = 0.f, y = 0.f;

		if (HasInputFlag(m_input.inputFlags, INPUT_FORWARD))		y += 1.f;
		if (HasInputFlag(m_input.inputFlags, INPUT_BACKWARD))		y -= 1.f;
		if (HasInputFlag(m_input.inputFlags, INPUT_LEFT))			x -= 1.f;
		if (HasInputFlag(m_input.inputFlags, INPUT_RIGHT))		x += 1.f;

		Vec2 dir = Vec2(x, y);
		dir.Normalize();

		intent.moveX   = dir.x;
		intent.moveY   = dir.y;
		intent.moveMag = 1.0f;

		intent.gaitRequest = HasInputFlag(m_input.inputFlags, INPUT_SPRINT) ? eGait::Sprint :
			HasInputFlag(m_input.inputFlags, INPUT_RUN) ? eGait::Run : eGait::Walk;

		intent.stanceRequest = HasInputFlag(m_input.inputFlags, INPUT_PRONE) ? eStance::Prone :
			HasInputFlag(m_input.inputFlags, INPUT_CROUCH) ? eStance::Crouching : eStance::Standing;

		intent.jumpPressed = HasInputFlag(m_input.inputFlags, INPUT_JUMP);
		intent.dashPressed = HasInputFlag(m_input.inputFlags, INPUT_DASH);

		return intent;
	}
}
