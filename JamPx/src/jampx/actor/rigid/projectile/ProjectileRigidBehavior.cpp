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

		void ApplyAuthorityState(ProjectileComponent* projectile, const RigidState& state)
		{
			if (!projectile) return;

			ProjectileState s = projectile->GetState();

			// 위치/회전 기준점은 권한 상태로 맞추되
			s.position = ToPhysX(state.pose.p);

			// 속도는 0 덮어쓰기 방지 (초기 velocity 보존)
			constexpr float velEps = EPS_3;
			if (state.linVel.MagnitudeSquared() > (velEps * velEps))
			{
				s.velocity = ToPhysX(state.linVel);
			}

			projectile->SetState(s);
		}


		void BuildRigidStateFromProjectile(const ProjectileComponent* projectile, const RigidState& prev, OUT RigidState& out)
		{
			out = prev;
			if (!projectile) return;

			const ProjectileState& ps = projectile->GetState();
			out.pose.p = ToPx(ps.position);
			out.linVel = ToPx(ps.velocity);
			out.angVel = Vec3::Zero();
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
		const bool wasHit = m_lastHitResult.hit;
		const bool wasLifetime = m_lastHitResult.maxRangeReached || m_lastHitResult.maxLifetimeReached;
        m_lastHitResult = m_mainProjectile->Tick(dt, scene, dyn);

		if (m_lastHitResult.hit && !wasHit)
			m_mainHitEventConsumed = false;

		if ((m_lastHitResult.maxRangeReached || m_lastHitResult.maxLifetimeReached) && !wasLifetime)
			m_mainLifetimeConsumed = false;
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
		RigidState now = prev;

		if (IsLocalDrivenProjectile(m_mainProjectile.get()))
		{
			BuildRigidStateFromProjectile(m_mainProjectile.get(), prev, now);
			now.pose.q = ToPx(dyn->getGlobalPose().q); // 회전은 actor 기준 유지
		}
		else
		{
			const PxTransform prevPose = ToPhysX(prev.pose);
			const PxTransform nowPose = dyn->getGlobalPose();

			now.pose = ToPx(nowPose);
			now.linVel = ToPx(GetLinearVelocity(nowPose.p, prevPose.p, m_lastDtMain));
			now.angVel = ToPx(GetAngularVelocity(nowPose.q, prevPose.q, m_lastDtMain));
		}

		body.SetMainState(now);

		const bool changed = IsMeaningfulChanged(prev, now);
		return changed || m_lastHitResult.IsTerminal() || m_lastHitResult.hit;
	}


	void ProjectileRigidBehavior::SyncReplayState(RigidBody& body)
	{
		auto* dyn = body.GetReplayActor()->is<PxRigidDynamic>();
		if (!dyn) return;

		const RigidState prev = body.GetReplayState();
		RigidState now = prev;

		if (IsLocalDrivenProjectile(m_replayProjectile.get()))
		{
			BuildRigidStateFromProjectile(m_replayProjectile.get(), prev, now);
			now.pose.q = ToPx(dyn->getGlobalPose().q);
		}
		else
		{
			const PxTransform prevPose = ToPhysX(prev.pose);
			const PxTransform nowPose = dyn->getGlobalPose();

			now.pose = ToPx(nowPose);
			now.linVel = ToPx(GetLinearVelocity(nowPose.p, prevPose.p, m_lastDtReplay));
			now.angVel = ToPx(GetAngularVelocity(nowPose.q, prevPose.q, m_lastDtReplay));
		}

		body.SetReplayState(now);
	}

	bool ProjectileRigidBehavior::ApplyMainState(const RigidState& state)
	{
		ApplyAuthorityState(m_mainProjectile.get(), state);
		return false;
	}

	bool ProjectileRigidBehavior::ApplyReplayState(const RigidState& state)
	{
		ApplyAuthorityState(m_replayProjectile.get(), state);
		return false;
	}

	bool ProjectileRigidBehavior::ConsumeMainHitEvent(OUT ProjectileHitResult& result)
	{
		if (!m_lastHitResult.hit || m_mainHitEventConsumed)
			return false;

		result = m_lastHitResult;
		m_mainHitEventConsumed = true;
		return true;
	}

	bool ProjectileRigidBehavior::ConsumeMainLifetimeEvent(OUT ProjectileHitResult& result)
	{
		if ((!(m_lastHitResult.maxRangeReached || m_lastHitResult.maxLifetimeReached)) || m_mainLifetimeConsumed)
			return false;

		result = m_lastHitResult;
		m_mainLifetimeConsumed = true;
		return true;
	}
} // namespace jam::px
