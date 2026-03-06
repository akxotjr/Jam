#pragma once


namespace jam::px
{
	class IKinematicDriver
	{
	public:
		virtual ~IKinematicDriver() = default;

		virtual PxTransform	Tick(float dt) = 0;
		virtual bool		IsDone() const { return false; }
	};
}
