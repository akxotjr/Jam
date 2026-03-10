#pragma once
#include "jampx/PhysXTypes.h"


namespace jam::px
{
	class IKinematicDriver
	{
	public:
		virtual ~IKinematicDriver() = default;

		virtual PxTransform		Tick(float dt) = 0;
		virtual bool			IsDone() const { return false; }
	};

} // namespace jam::px
