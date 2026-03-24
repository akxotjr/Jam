#include "pch.h"
#include "jampx/actor/rigid/projectile/ProjectileRigidBehavior.h"
#include "jampx/actor/rigid/RigidBody.h"

namespace jam::px
{
	ProjectileRigidBehavior::ProjectileRigidBehavior(
		std::unique_ptr<ProjectileComponent> mainProjectile,
		std::unique_ptr<ProjectileComponent> replayProjectile)
			: m_mainProjectile(std::move(mainProjectile)), m_replayProjectile(std::move(replayProjectile))
	{
	}

	void ProjectileRigidBehavior::SetTargetResolver(ProjectileTargetResolver resolver)
	{
		if (!m_mainProjectile || !m_replayProjectile)
			return;

		auto copied = resolver;
		m_mainProjectile->SetTargetResolver(std::move(resolver));
		m_replayProjectile->SetTargetResolver(std::move(copied));
	}


	void ProjectileRigidBehavior::TickOnMain(RigidBody& body, float dt)
	{
        if (!m_mainProjectile) return;

        auto* dyn = body.GetMainActor()->is<PxRigidDynamic>();
        if (!dyn) return;

        const PxScene* scene = dyn->getScene();

		m_lastDtMain    = dt;
        m_lastHitResult = m_mainProjectile->Tick(dt, scene, dyn);
	}

	void ProjectileRigidBehavior::TickOnReplay(RigidBody& body, float dt)
	{
		if (!m_replayProjectile) return;

		auto* dyn = body.GetReplayActor()->is<PxRigidDynamic>();
		if (!dyn) return;

		const PxScene* scene = dyn->getScene();

		m_lastDtReplay = dt;
		m_replayProjectile->Tick(dt, scene, dyn);
	}

	bool ProjectileRigidBehavior::SyncMainState(RigidBody& body)
	{
		auto* dyn = body.GetMainActor()->is<PxRigidDynamic>();
		if (!dyn) return false;

		RigidState state = body.GetMainState();
		const PxTransform prevPose = ToPhysX(state.pose);
		const PxTransform nowPose  = dyn->getGlobalPose();

		const PxVec3 linVel = GetLinearVelocity(nowPose.p, prevPose.p, m_lastDtMain);
		const PxVec3 angVel = GetAngularVelocity(nowPose.q, prevPose.q, m_lastDtMain);

		state.pose	 = ToPx(nowPose);
		state.linVel = ToPx(linVel);
		state.angVel = ToPx(angVel);

		body.SetMainState(state);
	
		return true;
	}


	void ProjectileRigidBehavior::SyncReplayState(RigidBody& body)
	{
		auto* dyn = body.GetReplayActor()->is<PxRigidDynamic>();
		if (!dyn) return;

		RigidState state = body.GetReplayState();
		const PxTransform prevPose = ToPhysX(state.pose);
		const PxTransform nowPose  = dyn->getGlobalPose();

		state.pose   = ToPx(nowPose);
		state.linVel = ToPx(GetLinearVelocity(nowPose.p, prevPose.p, m_lastDtReplay));
		state.angVel = ToPx(GetAngularVelocity(nowPose.q, prevPose.q, m_lastDtReplay));

		body.SetReplayState(state);
	}


} // namespace jam::px
