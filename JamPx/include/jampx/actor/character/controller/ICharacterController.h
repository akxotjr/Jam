#pragma once
#include "jampx/actor/character/CharacterMovementTypes.h"


namespace jam::px
{
	enum class eCharacterControlType : uint8
	{
		None, 
		Player,
		AI
	};

	class ICharacterController
	{
	public:
		virtual ~ICharacterController() = default;

		virtual MoveIntent				BuildIntent(float dt) = 0;
		virtual eCharacterControlType	GetType() const { return eCharacterControlType::None; }
	};
} // namespace jam::px
