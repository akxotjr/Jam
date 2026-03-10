#include "pch.h"
#include "jampx/kinematic/KinematicDriverComponent.h"


namespace jam::px
{
	KinematicDriverComponent::KinematicDriverComponent(std::unique_ptr<IKinematicDriver> driver)
		: m_driver(std::move(driver))
	{
	}

	PxTransform KinematicDriverComponent::Tick(float dt)
	{
		if (!m_driver) return {};
		return m_driver->Tick(dt);
	}

	bool KinematicDriverComponent::IsDone() const
	{
		return m_driver ? m_driver->IsDone() : true;
	}
}
