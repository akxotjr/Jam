#include "pch.h"
#include "jampx/projectile/ProjectileMoveComponent.h"


namespace jam::px
{
	ProjectileMoveComponent::ProjectileMoveComponent(const ProjectileMoveConfig& cfg, const Vec3& initialVel)
		: m_config(cfg)
	{
		m_state.velocity = initialVel;
	}

	ProjectileHitResult ProjectileMoveComponent::Tick(float dt, PxScene* scene, PxRigidDynamic* actor, uint16 teamId)
	{
		ProjectileHitResult result{};
		if (!scene || !actor) return result;

		const float gravity = scene->getGravity().y;

		m_state.velocity.y -= gravity * m_config.gravityScale * dt;

		const Vec3  disp = m_state.velocity * dt;
		const float dist = disp.Magnitude();
		if (dist < EPSILON) return result;

		const Vec3 normDir = disp * (1.f / dist);
		const PxTransform pose = actor->getGlobalPose();

		RequestQueryFD reqFD = MakeRequestQueryFD(
			QueryCategory::CHARACTER | QueryCategory::HITBOX | QueryCategory::WORLD,
			0, QuerySublayer::Default, 0,
			teamId, 0, 0,
			RequestQueryFlag::IGNORE_TRIGGERS | RequestQueryFlag::IGNORE_SAME_TEAM
		);
		QueryFilterCallbackT<> cb{ DefaultQueryPolicy{}, k_LOSQueryHitTypeMap };
		const PxQueryFilterData fd = MakePxQueryFilterData(reqFD);

		// Shape 존재 시 Sweep, 없으면 Raycast fallback
		PxShape* shape = nullptr;
		bool hasSweepShape = actor->getNbShapes() > 0 && actor->getShapes(&shape, 1) > 0 && shape != nullptr;

		bool	 hit	   = false;
		PxVec3	 hitPos	   = PxVec3(PxZero);
		PxVec3	 hitNormal = PxVec3(PxZero);
		ObjectId hitId     = INVALID_OBJ_ID;

		if (hasSweepShape)
		{
			PxSweepBuffer buf;
			hit = scene->sweep(shape->getGeometry().getType(), pose, ToPhysX(normDir), dist, buf, PxHitFlag::eDEFAULT, fd, &cb);
			if (hit && buf.hasBlock)
			{
				hitPos    = buf.block.position;
				hitNormal = buf.block.normal;
				hitId	  = GetObjectId(buf.block.actor);
			}
		}
		else
		{
			PxRaycastBuffer buf;
			hit = scene->raycast(pose.p, ToPhysX(normDir), dist, buf, PxHitFlag::eDEFAULT, fd, &cb);
			if (hit && buf.hasBlock)
			{
				hitPos    = buf.block.position;
				hitNormal = buf.block.normal;
				hitId	  = GetObjectId(buf.block.actor);
			}
		}

		if (hit)
		{
			result.hit		= true;
			result.position = ToPx(hitPos);
			result.normal	= ToPx(hitNormal);
			result.hitId	= hitId;
			return result;
		}

		// 이동 적용
		PxTransform newPose = pose;
		newPose.p += ToPhysX(disp);
		actor->setKinematicTarget(newPose);

		m_state.traveledDist += dist;
		if (m_state.traveledDist >= m_config.maxRange)
			result.maxRangeReached = true;

		return result;

	}
}
