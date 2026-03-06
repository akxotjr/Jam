#pragma once
#include "jampx/character/IAccelerator.h"


namespace jam::px
{
	class DefaultQuakeAccelerator : public IAccelerator
	{
	public:
		explicit DefaultQuakeAccelerator(const CharacterMoveConfig& cfg);

		WishMovement	BuildWishMovement(const MoveIntent& intent) const override;
		void			Integrate(CharacterMoveState& st, const WishMovement& wish, float dt) const override;

	private:
		CharacterMoveConfig	m_cfg{};
	};
}
