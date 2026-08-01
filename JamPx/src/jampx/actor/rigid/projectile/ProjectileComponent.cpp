#include "pch.h"
#include "jampx/actor/rigid/projectile/ProjectileComponent.h"
#include "jampx/PhysicsUtils.h"

namespace jam::px
{
	namespace
	{
		QueryFilterCallbackT<> MakeDefaultQueryCallback(const PxRigidActor* selfActor)
		{
			DefaultQueryPolicy policy{};
			policy.selfActor = selfActor;
			return QueryFilterCallbackT<>{ policy, k_ProjectileQueryHitTypeMap };
		}

		float ComputeAutoSphereRadius(const PxRigidDynamic* actor)
		{
			if (!actor) return 1.0f;

			const PxBounds3 bounds = actor->getWorldBounds(1.0f);
			const float r = bounds.getExtents().magnitude();
			return (r > EPSILON) ? r : 1.0f;
		}

		float ComputeExpansion(float dist)
		{
			// 설정 필드가 아직 없으므로, 이동 거리 기반의 보수적 팽창값 사용
			return physx::PxMax(0.01f, dist * 0.1f);
		}

		float NextSpeed(const ProjectileHomingConfig& cfg, float currentSpeed, float dt)
		{
			if (cfg.keepSpeedConstant)
			{
				if (currentSpeed > EPSILON)
					return physx::PxMin(currentSpeed, cfg.maxSpeed);
				return physx::PxMin(cfg.maxSpeed, cfg.acceleration * dt);
			}

			const float accelerated = currentSpeed + cfg.acceleration * dt;
			return physx::PxMin(accelerated, cfg.maxSpeed);
		}

		bool SweepWithActorShape(
			const PxScene* scene,
			const PxRigidDynamic* actor,
			const PxTransform& actorPose,
			const PxVec3& unitDir,
			float dist,
			float inflation,
			const PxQueryFilterData& fd,
			PxQueryFilterCallback* cb,
			OUT PxSweepBuffer& buf)
		{
			if (!scene || !actor)
				return false;

			PxShape* shape = nullptr;
			const bool hasShape = actor->getNbShapes() > 0 && actor->getShapes(&shape, 1) > 0 && shape != nullptr;
			if (!hasShape) return false;

			const PxGeometryHolder geom = shape->getGeometry();
			const PxTransform shapePose = actorPose.transform(shape->getLocalPose());

			return scene->sweep(geom.any(), shapePose, unitDir, dist, buf, PxHitFlag::eDEFAULT, fd, cb, nullptr, inflation);
		}

		bool SweepWithSphere(
			const PxScene* scene,
			const PxTransform& pose,
			const PxVec3& unitDir,
			float dist,
			float radius,
			const PxQueryFilterData& fd,
			PxQueryFilterCallback* cb,
			OUT PxSweepBuffer& buf)
		{
			if (!scene) return false;

			const PxSphereGeometry sphereGeom(physx::PxMax(radius, 0.01f));
			return scene->sweep(sphereGeom, pose, unitDir, dist, buf, PxHitFlag::eDEFAULT, fd, cb);
		}

		bool RaycastFallback(
			const PxScene* scene,
			const PxTransform& pose,
			const PxVec3& unitDir,
			float dist,
			const PxQueryFilterData& fd,
			PxQueryFilterCallback* cb,
			OUT PxRaycastBuffer& buf)
		{
			if (!scene) return false;

			return scene->raycast(pose.p, unitDir, dist, buf, PxHitFlag::eDEFAULT, fd, cb);
		}

		bool WriteSweepHit(const PxSweepBuffer& buf, OUT ProjectileHitResult& result)
		{
			if (!buf.hasBlock) return false;

			if (buf.block.distance <= EPSILON)
				return false;

			result.hit		= true;
			result.position	= buf.block.position;
			result.normal	= buf.block.normal;
			result.hitActorId	= GetActorId(buf.block.actor);
			return true;
		}

		bool WriteRayHit(const PxRaycastBuffer& buf, OUT ProjectileHitResult& result)
		{
			if (!buf.hasBlock) return false;

			if (buf.block.distance <= EPSILON)
				return false;

			result.hit		= true;
			result.position	= buf.block.position;
			result.normal	= buf.block.normal;
			result.hitActorId	= GetActorId(buf.block.actor);

			return true;
		}
	}

	ProjectileComponent::ProjectileComponent(const ProjectileConfig& cfg)
		: m_config(cfg)
	{
		m_state.velocity = m_config.motion.initialVelocity;
	}


	void ProjectileComponent::IntegrateMotion(float dt, const PxVec3& sceneGravity, OUT PxVec3& disp)
	{
		disp = PxVec3(physx::PxZero);

		switch (m_config.motion.model)
		{
		case eProjectileMotionModel::Linear:
		{
			disp = m_state.velocity * dt;
			break;
		}
		case eProjectileMotionModel::Ballistic:
		{
			const PxVec3 gravity = sceneGravity * m_config.motion.gravityScale;
			m_state.velocity += gravity * dt;
			disp = m_state.velocity * dt;
			break;
		}
		case eProjectileMotionModel::HomingSteer:
		{
			IntegrateHomingSteer(dt, disp);
			break;
		}
		case eProjectileMotionModel::HomingLead:
		{
			IntegrateHomingLead(dt, disp);
			break;
		}
		case eProjectileMotionModel::HomingPN:
		{
			IntegrateHomingPN(dt, disp);
			break;
		}
		default:
			break;
		}

		m_state.age += dt;
	}

	// -------------------------------------------------------------------------
	// HomingSteer
	//
	// 현재 target 위치를 향해 desiredDir를 만들고,
	// 현재 진행 방향을 maxTurnRateRad 한도 내에서 회전시킨다.
	// -------------------------------------------------------------------------
	void ProjectileComponent::IntegrateHomingSteer(float dt, OUT PxVec3& disp)
	{
		disp = m_state.velocity * dt;

		ProjectileHomingTarget target{};
		if (!ResolveHomingTarget(target))
		{
			if (!m_config.homing.keepLastDirection)
			{
				m_state.velocity = PxVec3(physx::PxZero);
				disp = PxVec3(physx::PxZero);
			}
			return;
		}
		PxVec3 toTarget = target.position - m_state.position;

		const PxVec3 desiredDir = SafeNormalize(toTarget);
		const PxVec3 currentDir = SafeNormalize(m_state.velocity, desiredDir);
		const PxVec3 newDir		= RotateTowards(currentDir, desiredDir, m_config.homing.maxTurnRate * dt);

		const PxReal nexSpeed	= NextSpeed(m_config.homing, m_state.velocity.magnitude(), dt);
		m_state.velocity = newDir * nexSpeed;

		disp = m_state.velocity * dt;
	}

	// -------------------------------------------------------------------------
	// HomingLead
	//
    //  analytic intercept
	//  fallback: futurePos = targetPos + targetVel * predictTime 를 향해 steering
	// -------------------------------------------------------------------------
	void ProjectileComponent::IntegrateHomingLead(float dt, OUT PxVec3& disp)
	{
		disp = m_state.velocity * dt;

		ProjectileHomingTarget target{};
		if (!ResolveHomingTarget(target))
		{
			if (!m_config.homing.keepLastDirection)
			{
				m_state.velocity = PxVec3(physx::PxZero);
				disp = PxVec3(physx::PxZero);
			}
			return;
		}

		const float  nextSpeed  = NextSpeed(m_config.homing, m_state.velocity.magnitude(), dt);
		const PxVec3 currentDir = SafeNormalize(m_state.velocity, SafeNormalize(target.position - m_state.position));

		float  interceptTime  = 0.0f;
		PxVec3 interceptPoint = PxVec3(physx::PxZero);

		bool hasIntercept = SolveIntercept(
			m_state.position,
			PxVec3(physx::PxZero),	// ignore the existing speed added to the projectile
			target.position,
			target.velocity,
			nextSpeed,
			m_config.homing.maxPredictTime,
			interceptTime,
			interceptPoint);

		PxVec3 desiredDir = PxVec3(physx::PxZero);

		if (hasIntercept)
		{
			desiredDir = SafeNormalize(interceptPoint - m_state.position, currentDir);
		}
		else
		{
			const float	 speed      = physx::PxMax(m_state.velocity.magnitude(), 0.01f);
			const PxVec3 toNow      = target.position - m_state.position;
			const float  distance   = toNow.magnitude();

			const float timeToGo    = distance / speed;
			const float predictTime = physx::PxMin(m_config.homing.maxPredictTime, timeToGo * m_config.homing.leadTimeScale);
		
			const PxVec3 futurePos = target.position + target.velocity * predictTime;
			desiredDir = SafeNormalize(futurePos - m_state.position, currentDir);
		}

		const PxVec3 newDir	= RotateTowards(currentDir, desiredDir, m_config.homing.maxTurnRate * dt);

		m_state.velocity = newDir * nextSpeed;
		disp = m_state.velocity * dt;
	}


	// -------------------------------------------------------------------------
	// HomingPN
	//
	// True Proportional Navigation.
	//
	// r      = targetPos - missilePos
	// vRel   = targetVel - missileVel
	// omega  = (r x vRel) / |r|^2
	// accel  = N * (vel x omega)
	//
	// accel은 velocity와 수직인 횡가속으로 작용.
	// -------------------------------------------------------------------------
	void ProjectileComponent::IntegrateHomingPN(float dt, OUT PxVec3& disp)
	{
		disp = m_state.velocity * dt;

		ProjectileHomingTarget target{};
		if (!ResolveHomingTarget(target))
		{
			if (!m_config.homing.keepLastDirection)
			{
				m_state.velocity = PxVec3(physx::PxZero);
				disp = PxVec3(physx::PxZero);
			}
			return;
		}

		const PxVec3 r = target.position - m_state.position;
		const float r2 = r.magnitudeSquared();
		if (r2 <= EPSILON * EPSILON)
			return;

		const PxVec3 vRel  = target.velocity - m_state.velocity;
		const PxVec3 omega = r.cross(vRel) * (1.f / r2);

		PxVec3 aLat = m_state.velocity.cross(omega) * m_config.homing.navigationGain;
		aLat = ClampMagnitude(aLat, m_config.homing.maxLateralAccel);

		const PxVec3 desiredVel = m_state.velocity + aLat * dt;
		const PxVec3 desiredDir = SafeNormalize(desiredVel, SafeNormalize(r));
		const PxVec3 currentDir = SafeNormalize(m_state.velocity, desiredDir);

		const PxVec3 newDir     = RotateTowards(currentDir, desiredDir, m_config.homing.maxTurnRate * dt);
		const float  nextSpeed  = NextSpeed(m_config.homing, m_state.velocity.magnitude(), dt);

		m_state.velocity = newDir * nextSpeed;
		disp = m_state.velocity * dt;
	}

	bool ProjectileComponent::ResolveHomingTarget(ProjectileHomingTarget& target) const
	{
		if (!m_config.homing.enableHoming)
			return false;

		if (!m_resolver)
			return false;

		if (m_config.homing.targetActorId == INVALID_ACTOR_ID)
			return false;

		if (!m_resolver(m_config.homing.targetActorId, target))
			return false;

		return target.valid;
	}

	bool ProjectileComponent::CheckLifetime(OUT ProjectileHitResult& result) const
	{
		if (m_state.traveledDist >= m_config.lifetime.maxRange)
		{
			result.maxRangeReached = true;
			return true;
		}

		if (m_state.age >= m_config.lifetime.maxLifetime)
		{
			result.maxLifetimeReached = true;
			return true;
		}

		return false;
	}


	bool ProjectileComponent::QueryHit(
		const PxScene* scene,
		const PxRigidDynamic* actor,
		const PxTransform& pose,
		const PxVec3& disp,
		OUT ProjectileHitResult& result) const
	{
		const float dist = disp.magnitude();
		if (!scene || !actor || dist < EPSILON)
			return false;

		const PxVec3 normDir = disp.getNormalized();

		const PxQueryFilterData fd = MakePxQueryFilterData(m_config.hit.requestFd);
		auto cb = MakeDefaultQueryCallback(actor);

		const bool  tryRayFallback   = m_config.hit.fallbackRaycast;
		const float baseSphereRadius = ComputeAutoSphereRadius(actor);
		const float expansion		 = ComputeExpansion(dist);

		switch (m_config.hit.model)
		{
		case eProjectileHitModel::RaycastFallback:
		{
			PxRaycastBuffer buf;
			if (RaycastFallback(scene, pose, normDir, dist, fd, &cb, buf) && WriteRayHit(buf, result))
				return true;
			break;
		}
		case eProjectileHitModel::ShapeSweep:
		{
			if (m_config.hit.useShapeSweep)
			{
				PxSweepBuffer buf;
				if (SweepWithActorShape(scene, actor, pose, normDir, dist, 0.f, fd, &cb, buf) && WriteSweepHit(buf, result))
					return true;

			}

			if (tryRayFallback)
			{
				PxRaycastBuffer buf;
				if (RaycastFallback(scene, pose, normDir, dist, fd, &cb, buf) && WriteRayHit(buf, result))
					return true;
			}
			break;
		}
		case eProjectileHitModel::SphereSweep:
		{
			PxSweepBuffer buf;
			if (SweepWithSphere(scene, pose, normDir, dist, baseSphereRadius, fd, &cb, buf) && WriteSweepHit(buf, result))
				return true;

			if (tryRayFallback)
			{
				PxRaycastBuffer rb;
				if (RaycastFallback(scene, pose, normDir, dist, fd, &cb, rb) && WriteRayHit(rb, result))
					return true;
			}
			break;
		}
		case eProjectileHitModel::ExpandingShapeSweep:
		{
			if (m_config.hit.useShapeSweep)
			{
				PxSweepBuffer buf;
				if (SweepWithActorShape(scene, actor, pose, normDir, dist, expansion, fd, &cb, buf) && WriteSweepHit(buf, result))
					return true;
			}

			if (tryRayFallback)
			{
				PxRaycastBuffer rb;
				if (RaycastFallback(scene, pose, normDir, dist, fd, &cb, rb) && WriteRayHit(rb, result))
					return true;
			}
			break;
		}
		case eProjectileHitModel::ExpandingSphereSweep:
		{
			PxSweepBuffer buf;
			if (SweepWithSphere(scene, pose, normDir, dist, baseSphereRadius + expansion, fd, &cb, buf) && WriteSweepHit(buf, result))
				return true;

			if (tryRayFallback)
			{
				PxRaycastBuffer rb;
				if (RaycastFallback(scene, pose, normDir, dist, fd, &cb, rb) && WriteRayHit(rb, result))
					return true;
			}
			break;
		}
		default:
			break;
		}

		return false;
	}

	ProjectileHitResult ProjectileComponent::Tick(float dt, const PxScene* scene, PxRigidDynamic* actor)
	{
		ProjectileHitResult result{};
		if (!scene || !actor || dt <= 0.f)
			return result;


		const PxTransform pose = actor->getGlobalPose();
		m_state.position = pose.p;

		PxVec3 disp;
		IntegrateMotion(dt, scene->getGravity(), disp);

		const float dist = disp.magnitude();
		if (dist < EPSILON)
		{
			CheckLifetime(result);
			return result;
		}

		if (QueryHit(scene, actor, pose, disp, result))
		{
			if (result.hit)
			{
				m_state.traveledDist += (result.position - m_state.position).magnitude();
				m_state.position	  = result.position;
			}
			return result;
		}

		PxTransform newPose = pose;
		newPose.p += disp;

		actor->setKinematicTarget(newPose);

		m_state.position	 += disp;
		m_state.traveledDist += dist;

		CheckLifetime(result);
		return result;
	}

} // namespace jam::px
