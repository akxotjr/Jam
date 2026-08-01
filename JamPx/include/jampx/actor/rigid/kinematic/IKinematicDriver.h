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

		virtual KinematicState  BuildState() const { return {}; }

		/// @brief runtime binding hook for eKineDrivenType == TargetDerived
		virtual bool			SetTargetId(ActorId oid) { return false; }
	};

} // namespace jam::px
