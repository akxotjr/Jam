#pragma once


namespace jam::px
{
	class IKinematicDriver
	{
	public:
		virtual ~IKinematicDriver() = default;

		virtual physx::PxTransform	Tick(float dt) = 0;
		virtual bool		IsDone() const { return false; }
	};
}
