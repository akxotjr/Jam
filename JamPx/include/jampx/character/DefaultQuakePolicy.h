#pragma once
#include "jampx/character/IAccelerationPolicy.h"


namespace jam::px
{
	class DefaultQuakePolicy : public IAccelerationPolicy
	{
	public:
		explicit DefaultQuakePolicy(const MovementConfig& cfg);

		WishMovement	BuildWishMovement(const MoveIntent& intent) const override;
		void			Integrate(MovementState& st, const WishMovement& wish, float dt) const override;

	private:
		MovementConfig	m_cfg{};
	};
}
