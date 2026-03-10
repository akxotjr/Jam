#include "pch.h"
#include "jampx/actor/rigid/projectile/ProjectileRigidBehavior.h"
#include "jampx/actor/rigid/RigidBody.h"

namespace jam::px
{
	ProjectileRigidBehavior::ProjectileRigidBehavior(std::unique_ptr<ProjectileComponent> projectile)
		: m_projectile(std::move(projectile))
	{
	}

	void ProjectileRigidBehavior::Tick(RigidBody& body, float dt)
	{
        if (!m_projectile)
            return;

        auto* dyn = body.GetActor()->is<PxRigidDynamic>();
        if (!dyn)
            return;

        PxScene* scene = dyn->getScene();
        m_lastHitResult = m_projectile->Tick(dt, scene, dyn, m_teamId);

        // Actor로부터 상태 동기화
        RigidState state = body.GetState();
        state.pose = ToPx(dyn->getGlobalPose());
        state.linVel = ToPx(dyn->getLinearVelocity());
        state.angVel = ToPx(dyn->getAngularVelocity());
        body.SetState(state);
	}


} // namespace jam::px
