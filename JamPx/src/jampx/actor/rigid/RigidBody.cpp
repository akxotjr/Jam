#include "pch.h"
#include "jampx/actor/rigid/RigidBody.h"


namespace jam::px
{

	RigidBody::RigidBody(PxRigidActor* actor, std::unique_ptr<IRigidBehavior> behavior)
		: m_actor(actor), m_behavior(std::move(behavior))
	{
	}

	void RigidBody::Tick(float dt)
	{
		if (m_behavior)
			m_behavior->Tick(*this, dt);
	}

	PhysicsHandle RigidBody::GetPhysicsHandle()
	{
		return PhysicsHandle{ reinterpret_cast<uint64_t>(m_actor) };
	}


} // namespace jam::px
