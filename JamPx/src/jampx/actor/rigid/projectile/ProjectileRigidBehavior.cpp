#include "pch.h"
#include "jampx/actor/rigid/projectile/ProjectileRigidBehavior.h"
#include "jampx/actor/rigid/RigidBody.h"

namespace jam::px
{

	namespace
	{
		bool IsLocalDrivenProjectile(const ProjectileComponent* proj)
		{
			if (!proj) return false;

			const eProjectileKind kind = proj->GetConfig().kind;
			return kind != eProjectileKind::DYN_SIM;
		}

		bool IsMeaningfulChanged(const RigidState& prev, const RigidState& now)
		{
			constexpr float posEps    = EPS_3; 
			constexpr float rotEps    = EPS_4;
			constexpr float linVelEps = EPS_2;
			constexpr float angVelEps = EPS_2;

			if ((prev.pose.p - now.pose.p).MagnitudeSquared() > (posEps * posEps))
				return true;

			{
				const Quat a = prev.pose.q.GetNormalized();
				const Quat b = now.pose.q.GetNormalized();
				const float absDot = std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
				if (absDot < (1.0f - rotEps))
					return true;
			}

			if ((prev.linVel - now.linVel).MagnitudeSquared() > (linVelEps * linVelEps))
				return true;

			if ((prev.angVel - now.angVel).MagnitudeSquared() > (angVelEps * angVelEps))
				return true;

			return false;
		}

	} // anonymous namespace



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
		const auto* dyn = body.GetMainActor()->is<PxRigidDynamic>();
		if (!dyn) return false;

		const RigidState prev = body.GetMainState();

		const PxTransform prevPose = ToPhysX(prev.pose);
		const PxTransform nowPose  = dyn->getGlobalPose();

		RigidState now = prev;
		now.pose   = ToPx(nowPose);
		now.linVel = ToPx(GetLinearVelocity(nowPose.p, prevPose.p, m_lastDtMain));
		now.angVel = ToPx(GetAngularVelocity(nowPose.q, prevPose.q, m_lastDtMain));

		body.SetMainState(now);

		// LocalDriven projectile: lifetime/hit 같은 이벤트 시에만 dirty
		if (IsLocalDrivenProjectile(m_mainProjectile.get()))
			return m_lastHitResult.IsTerminal() || m_lastHitResult.hit;

		// Non-local-driven(DYN_SIM): 상태 변화 기반 dirty
		return IsMeaningfulChanged(prev, now);
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
