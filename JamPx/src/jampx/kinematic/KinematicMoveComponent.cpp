#include "pch.h"
#include "jampx/kinematic/KinematicMoveComponent.h"


namespace jam::px
{
	KinematicMoveComponent::KinematicMoveComponent(std::unique_ptr<IKinematicDriver> driver)
		: m_driver(std::move(driver))
	{
	}

	Transform KinematicMoveComponent::Tick(float dt)
	{
		if (!m_driver) return {};
		return m_driver->Tick(dt);
	}

	bool KinematicMoveComponent::IsDone() const
	{
		return m_driver ? m_driver->IsDone() : true;
	}
}
