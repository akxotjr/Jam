#include "pch.h"
#include "jampx/RigidBody.h"


namespace jam::px
{
	RigidBody::RigidBody(PxRigidActor* actor)
		: m_actor(actor)
	{
	}

	void RigidBody::AttachDriver(std::unique_ptr<IKinematicDriver> driver)
	{
		m_driver = std::make_unique<KinematicDriverComponent>(std::move(driver));
	}

	void RigidBody::DetachDriver()
	{
		m_driver.reset();
	}

	void RigidBody::Tick(float dt)
	{
		if (!m_driver || m_driver->IsDone())
			return;

		auto pose = m_driver->Tick(dt);

		if (auto* dyn = m_actor->is<PxRigidDynamic>())
		{
			dyn->setKinematicTarget(pose);
			m_state.pose	= ToPx(pose);
			m_state.linVel  = Vec3::Zero();	//todo
			m_state.angVel  = Vec3::Zero();
		}
	}

	PhysicsHandle RigidBody::GetPhysicsHandle()
	{
		return PhysicsHandle{ reinterpret_cast<uint64_t>(m_actor) };
	}


} // namespace jam::px
