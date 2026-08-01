#pragma once

#include "jampx/actor/character/controller/ICharacterController.h"

namespace jam::px
{
	class PlayerControllerComponent : public ICharacterController
	{
	public:
		void					SetInput(const CharacterMotorInput& input) { m_input = input; }
		MoveIntent				BuildIntent(float dt) override;
		eCharacterControlType	GetType() const override { return eCharacterControlType::Player; }

	private:
		CharacterMotorInput			m_input = {};
	};


} // namespace jam::px
