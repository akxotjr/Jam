#include "pch.h"
#include "jampx/actor/rigid/kinematic/KinematicRigidBehavior.h"
#include "jampx/actor/rigid/RigidBody.h"

namespace jam::px
{
	KinematicRigidBehavior::KinematicRigidBehavior(std::unique_ptr<IKinematicDriver> driver)
		: m_driver(std::move(driver))
	{
	}

	void KinematicRigidBehavior::Tick(RigidBody& body, float dt)
	{
        if (!m_driver || m_driver->IsDone())
            return;

        const PxTransform pose = m_driver->Tick(dt);

        if (auto* dyn = body.GetActor()->is<PxRigidDynamic>())
        {
            dyn->setKinematicTarget(pose);

            RigidState state = body.GetState();
            state.pose = ToPx(pose);
            state.linVel = Vec3::Zero();
            state.angVel = Vec3::Zero();
            body.SetState(state);
        }
	}
} // namespace jam::px

