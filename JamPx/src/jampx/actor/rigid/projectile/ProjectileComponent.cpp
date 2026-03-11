#include "pch.h"
#include "jampx/actor/rigid/projectile/ProjectileComponent.h"

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
			if (!buf.hasBlock)
				return false;

			result.hit		= true;
			result.position	= ToPx(buf.block.position);
			result.normal	= ToPx(buf.block.normal);
			result.hitId	= GetObjectId(buf.block.actor);
			return true;
		}

		bool WriteRayHit(const PxRaycastBuffer& buf, OUT ProjectileHitResult& result)
		{
			if (!buf.hasBlock)
				return false;

			result.hit		= true;
			result.position	= ToPx(buf.block.position);
			result.normal	= ToPx(buf.block.normal);
			result.hitId	= GetObjectId(buf.block.actor);

			return true;
		}
	}

	ProjectileComponent::ProjectileComponent(const ProjectileConfig& cfg)
		: m_config(cfg)
	{
		m_state.velocity = m_config.motion.initialVelocity;
	}

	RequestQueryFD ProjectileComponent::BuildRuntimeRequestFd(uint16 teamId, const PxRigidActor* selfActor) const
	{
		RequestQueryFD fd = m_config.hit.requestFd;
		fd.id = PackedId32::Make(teamId, 0, 0);

		if (selfActor) fd.flags |= RequestQueryFlag::IGNORE_SELF_ACTOR;

		return fd;
	}


	void ProjectileComponent::IntegrateMotion(float dt, const PxVec3& sceneGravity, Vec3& outDisp)
	{
		outDisp = Vec3::Zero();

		switch (m_config.motion.model)
		{
		case eProjectileMotionModel::Linear:
		{
			outDisp = m_state.velocity * dt;
			break;
		}
		case eProjectileMotionModel::Ballistic:
		{
			const Vec3 gravity = ToPx(sceneGravity) * m_config.motion.gravityScale;
			m_state.velocity += gravity * dt;
			outDisp = m_state.velocity * dt;
			break;
		}
		case eProjectileMotionModel::Homing:
		{
			// 현재 config/state에 target 정보가 없어 안전하게 Linear로 동작
			outDisp = m_state.velocity * dt;
			break;
		}
		default:
			break;
		}

		m_state.age += dt;
	}

	bool ProjectileComponent::CheckLifetime(ProjectileHitResult& outResult) const
	{
		if (m_state.traveledDist >= m_config.lifetime.maxRange)
		{
			outResult.maxRangeReached = true;
			return true;
		}

		if (m_state.age >= m_config.lifetime.maxLifetime)
		{
			outResult.maxLifetimeReached = true;
			return true;
		}

		return false;
	}


	bool ProjectileComponent::QueryHit(
		const PxScene* scene,
		const PxRigidDynamic* actor,
		const PxTransform& pose,
		const Vec3& disp,
		OUT ProjectileHitResult& result) const
	{
		const float dist = disp.Magnitude();
		if (!scene || !actor || dist < EPSILON)
			return false;

		const Vec3 normDir = disp * (1.f / dist);
		const PxVec3 pxDir = ToPhysX(normDir);

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
			if (RaycastFallback(scene, pose, pxDir, dist, fd, &cb, buf) && WriteRayHit(buf, result))
				return true;
			break;
		}
		case eProjectileHitModel::ShapeSweep:
		{
			if (m_config.hit.useShapeSweep)
			{
				PxSweepBuffer buf;
				if (SweepWithActorShape(scene, actor, pose, pxDir, dist, 0.f, fd, &cb, buf) && WriteSweepHit(buf, result))
					return true;
			}

			if (tryRayFallback)
			{
				PxRaycastBuffer buf;
				if (RaycastFallback(scene, pose, pxDir, dist, fd, &cb, buf) && WriteRayHit(buf, result))
					return true;
			}
			break;
		}
		case eProjectileHitModel::SphereSweep:
		{
			PxSweepBuffer buf;
			if (SweepWithSphere(scene, pose, pxDir, dist, baseSphereRadius, fd, &cb, buf) && WriteSweepHit(buf, result))
				return true;

			if (tryRayFallback)
			{
				PxRaycastBuffer rb;
				if (RaycastFallback(scene, pose, pxDir, dist, fd, &cb, rb) && WriteRayHit(rb, result))
					return true;
			}
			break;
		}
		case eProjectileHitModel::ExpandingShapeSweep:
		{
			if (m_config.hit.useShapeSweep)
			{
				PxSweepBuffer buf;
				if (SweepWithActorShape(scene, actor, pose, pxDir, dist, expansion, fd, &cb, buf) && WriteSweepHit(buf, result))
					return true;
			}

			if (tryRayFallback)
			{
				PxRaycastBuffer rb;
				if (RaycastFallback(scene, pose, pxDir, dist, fd, &cb, rb) && WriteRayHit(rb, result))
					return true;
			}
			break;
		}
		case eProjectileHitModel::ExpandingSphereSweep:
		{
			PxSweepBuffer buf;
			if (SweepWithSphere(scene, pose, pxDir, dist, baseSphereRadius + expansion, fd, &cb, buf) && WriteSweepHit(buf, result))
				return true;

			if (tryRayFallback)
			{
				PxRaycastBuffer rb;
				if (RaycastFallback(scene, pose, pxDir, dist, fd, &cb, rb) && WriteRayHit(rb, result))
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
		m_state.position = ToPx(pose.p);

		Vec3 disp = Vec3::Zero();
		IntegrateMotion(dt, scene->getGravity(), disp);

		const float dist = disp.Magnitude();
		if (dist < EPSILON)
		{
			CheckLifetime(result);
			return result;
		}

		if (QueryHit(scene, actor, pose, disp, result))
		{
			if (result.hit)
			{
				m_state.traveledDist += (result.position - m_state.position).Magnitude();
				m_state.position	  = result.position;
			}
			return result;
		}

		PxTransform newPose = pose;
		newPose.p += ToPhysX(disp);

		actor->setKinematicTarget(newPose);

		m_state.position	 += disp;
		m_state.traveledDist += dist;

		CheckLifetime(result);
		return result;
	}

} // namespace jam::px
