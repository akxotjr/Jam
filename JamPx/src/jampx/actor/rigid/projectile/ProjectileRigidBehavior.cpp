#include "pch.h"
#include "jampx/actor/rigid/projectile/ProjectileRigidBehavior.h"
#include "jampx/actor/rigid/RigidBody.h"

namespace jam::px
{
	ProjectileRigidBehavior::ProjectileRigidBehavior(std::unique_ptr<ProjectileComponent> projectile)
		: m_projectile(std::move(projectile))
	{
	}

	void ProjectileRigidBehavior::SetTargetResolver(ProjectileTargetResolver resolver)
	{
		if (!m_projectile)
			return;

		m_projectile->SetTargetResolver(std::move(resolver));
	}

	void ProjectileRigidBehavior::Tick(RigidBody& body, float dt)
	{
        if (!m_projectile) return;

        auto* dyn = body.GetActor()->is<PxRigidDynamic>();
        if (!dyn) return;

        PxScene* scene = dyn->getScene();

		m_lastDt = dt;
        m_lastHitResult = m_projectile->Tick(dt, scene, dyn);
	}

	void ProjectileRigidBehavior::SyncState(RigidBody& body)
	{
		auto* dyn = body.GetActor()->is<PxRigidDynamic>();
		if (!dyn) return;

		RigidState state = body.GetState();
		const PxTransform prevPose	  = ToPhysX(state.pose);
		const PxTransform currentPose = dyn->getGlobalPose();

		const PxVec3 linVel = GetLinearVelocity(currentPose.p, prevPose.p, m_lastDt);
		const PxVec3 angVel = GetAngularVelocity(currentPose.q, prevPose.q, m_lastDt);

		state.pose	 = ToPx(currentPose);
		state.linVel = ToPx(linVel);
		state.angVel = ToPx(angVel);

		body.SetState(state, true);
	}
} // namespace jam::px
