#pragma once
#include "jampx/character/IAccelerator.h"


namespace jam::px
{
	class DefaultQuakeAccelerator : public IAccelerator
	{
	public:
		explicit DefaultQuakeAccelerator(const MovementConfig& cfg);

		WishMovement	BuildWishMovement(const MoveIntent& intent) const override;
		void			Integrate(MovementState& st, const WishMovement& wish, float dt) const override;

	private:
		MovementConfig	m_cfg{};
	};
}
