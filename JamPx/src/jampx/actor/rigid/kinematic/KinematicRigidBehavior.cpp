#include "pch.h"
#include "jampx/actor/rigid/kinematic/KinematicRigidBehavior.h"
#include "jampx/actor/rigid/RigidBody.h"
#include "jampx/actor/rigid/kinematic/KinematicDrivers.h"

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

		auto* dyn = body.GetActor()->is<PxRigidDynamic>();
		if (!dyn) return;

		m_lastDt = dt;
		const PxTransform pose = m_driver->Tick(dt);
		dyn->setKinematicTarget(pose);
	}

	void KinematicRigidBehavior::SyncState(RigidBody& body)
	{
		auto* dyn = body.GetActor()->is<PxRigidDynamic>();
		if (!dyn) return;

		RigidState state = body.GetState();
		const PxTransform prevPose    = ToPhysX(state.pose);
		const PxTransform currentPose = dyn->getGlobalPose();

		const PxVec3 linVel	= GetLinearVelocity(currentPose.p, prevPose.p, m_lastDt);
		const PxVec3 angVel	= GetAngularVelocity(currentPose.q, prevPose.q, m_lastDt);
		
		state.pose	 = ToPx(currentPose);
		state.linVel = ToPx(linVel);
		state.angVel = ToPx(angVel);

		body.SetState(state, true);
	}

	bool KinematicRigidBehavior::ApplyAuthoritativeState(const RigidState& s)
	{
		auto* net = dynamic_cast<NetworkPoseKinematicDriver*>(m_driver.get());
		if (!net) return false;

		net->SetAuthoritativePose(ToPhysX(s.pose));
		net->SetAuthoritativeLinearVelocity(ToPhysX(s.linVel));
		net->SetAuthoritativeAngularVelocity(ToPhysX(s.angVel));
		return true;
	}
} // namespace jam::px

