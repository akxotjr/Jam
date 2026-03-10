#pragma once


namespace jam::px
{
	class ICharacterController
	{
	public:
		virtual ~ICharacterController() = default;

		virtual MoveIntent BuildIntent(float dt) = 0;
	};
}
