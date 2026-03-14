#include "pch.h"
#include "jampx/actor/rigid/RigidBody.h"

#include "jampx/actor/rigid/kinematic/KinematicRigidBehavior.h"


namespace jam::px
{

	RigidBody::RigidBody(PxRigidActor* actor, std::unique_ptr<IRigidBehavior> behavior)
		: m_actor(actor), m_behavior(std::move(behavior))
	{
	}

	void RigidBody::Tick(float dt)
	{
		if (m_behavior) m_behavior->Tick(*this, dt);
	}

	void RigidBody::SyncState(RigidBody& body)
	{
		if (m_behavior) m_behavior->SyncState(body);
	}

	RigidState RigidBody::GetState() const
	{
		JAM_ASSERT(m_actor)

		if (m_behavior) return m_state;

		if (auto* dyn = m_actor->is<PxRigidDynamic>())
		{
			if (dyn->getRigidBodyFlags().isSet(PxRigidBodyFlag::eKINEMATIC))
				return m_state;

			RigidState out = m_state;
			out.pose   = ToPx(dyn->getGlobalPose());
			out.linVel = ToPx(dyn->getLinearVelocity());
			out.angVel = ToPx(dyn->getAngularVelocity());
			return out;
		}

		if (m_actor->is<PxRigidStatic>())
		{
			RigidState out = m_state;
			out.pose   = ToPx(m_actor->getGlobalPose());
			out.linVel = Vec3::Zero();
			out.angVel = Vec3::Zero();
			return out;
		}

		return m_state;
	}

	void RigidBody::SetState(const RigidState& s, bool kinematicLike)
	{
		m_state = s;
		if (!m_actor) return;

		if (kinematicLike && m_behavior && m_behavior->ApplyAuthoritativeState(s))
			return;

		if (auto* dyn = m_actor->is<PxRigidDynamic>())
		{
			if (kinematicLike)
			{
				dyn->setKinematicTarget(ToPhysX(s.pose));
			}
			else
			{
				dyn->setGlobalPose(ToPhysX(s.pose));
				dyn->setLinearVelocity(ToPhysX(s.linVel));
				dyn->setAngularVelocity(ToPhysX(s.angVel));
			}
		}
	}

	PhysicsHandle RigidBody::GetPhysicsHandle()
	{
		return PhysicsHandle{ reinterpret_cast<uint64_t>(m_actor) };
	}


} // namespace jam::px
