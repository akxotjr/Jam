#pragma once

#include "jampx/character/ICharacterController.h"

namespace jam::px
{


	class PlayerControllerComponent : public ICharacterController
	{
	public:
		void				SetInput(const CharacterInput& input) { m_input = input; }
		MoveIntent			BuildIntent(float dt) override;

	private:
		CharacterInput		m_input = {};
	};


}
