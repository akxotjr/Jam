#include "pch.h"
#include "jampx/actor/rigid/projectile/ProjectileComponent.h"


namespace jam::px
{
	namespace
	{
		static QueryFilterCallbackT<> MakeDefaultQueryCallback()
		{
			return QueryFilterCallbackT<>{ DefaultQueryPolicy{}, k_LOSQueryHitTypeMap };
		}

		static bool SweepWithActorShape(
			PxScene* scene,
			PxRigidDynamic* actor,
			const PxTransform& pose,
			const PxVec3& unitDir,
			float dist,
			const PxQueryFilterData& fd,
			PxQueryFilterCallback* cb,
			PxSweepBuffer& outBuf)
		{
			if (!scene || !actor)
				return false;

			PxShape* shape = nullptr;
			const bool hasShape = actor->getNbShapes() > 0 && actor->getShapes(&shape, 1) > 0 && shape != nullptr;
			if (!hasShape)
				return false;

			return scene->sweep(
				shape->getGeometry().getType(),
				pose,
				unitDir,
				dist,
				outBuf,
				PxHitFlag::eDEFAULT,
				fd,
				cb);
		}

		static bool RaycastFallback(
			PxScene* scene,
			const PxTransform& pose,
			const PxVec3& unitDir,
			float dist,
			const PxQueryFilterData& fd,
			PxQueryFilterCallback* cb,
			PxRaycastBuffer& outBuf)
		{
			if (!scene)
				return false;

			return scene->raycast(
				pose.p,
				unitDir,
				dist,
				outBuf,
				PxHitFlag::eDEFAULT,
				fd,
				cb);
		}
	}

	ProjectileComponent::ProjectileComponent(const ProjectileConfig& cfg)
		: m_config(cfg)
	{
		m_state.velocity = m_config.motion.initialVelocity;
	}

	void ProjectileComponent::InitializeFromActorIfNeeded(PxRigidDynamic* actor)
	{
		if (m_state.started || !actor)
			return;

		m_state.position = ToPx(actor->getGlobalPose().p);
		m_state.started = true;
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

	PxQueryFilterData ProjectileComponent::BuildQueryFilterData(uint16 teamId) const
	{
		RequestQueryFlag::Flags reqFlags = RequestQueryFlag::NONE;

		if (m_config.hit.ignoreTriggers)
			reqFlags |= RequestQueryFlag::IGNORE_TRIGGERS;
		if (m_config.hit.ignoreSameTeam)
			reqFlags |= RequestQueryFlag::IGNORE_SAME_TEAM;

		RequestQueryFD reqFD = MakeRequestQueryFD(
			m_config.hit.queryMask,
			0,
			QuerySublayer::Default,
			0,
			teamId,
			0,
			0,
			reqFlags);

		return MakePxQueryFilterData(reqFD);
	}

	bool ProjectileComponent::QueryHit(
		PxScene* scene,
		PxRigidDynamic* actor,
		const PxTransform& pose,
		const Vec3& disp,
		uint16 teamId,
		ProjectileHitResult& outResult) const
	{
		const float dist = disp.Magnitude();
		if (!scene || !actor || dist < EPSILON)
			return false;

		const Vec3  normDir = disp * (1.f / dist);
		const PxVec3 pxDir = ToPhysX(normDir);

		const PxQueryFilterData fd = BuildQueryFilterData(teamId);
		auto cb = MakeDefaultQueryCallback();

		bool hit = false;

		if (m_config.hit.model == eProjectileHitModel::ShapeSweep && m_config.hit.useShapeSweep)
		{
			PxSweepBuffer buf;
			hit = SweepWithActorShape(scene, actor, pose, pxDir, dist, fd, &cb, buf);
			if (hit && buf.hasBlock)
			{
				outResult.hit = true;
				outResult.position = ToPx(buf.block.position);
				outResult.normal = ToPx(buf.block.normal);
				outResult.hitId = GetObjectId(buf.block.actor);
				return true;
			}
		}

		if (m_config.hit.fallbackRaycast)
		{
			PxRaycastBuffer buf;
			hit = RaycastFallback(scene, pose, pxDir, dist, fd, &cb, buf);
			if (hit && buf.hasBlock)
			{
				outResult.hit = true;
				outResult.position = ToPx(buf.block.position);
				outResult.normal = ToPx(buf.block.normal);
				outResult.hitId = GetObjectId(buf.block.actor);
				return true;
			}
		}

		return false;
	}

	ProjectileHitResult ProjectileComponent::Tick(float dt, PxScene* scene, PxRigidDynamic* actor, uint16 teamId)
	{
		ProjectileHitResult result{};
		if (!scene || !actor || dt <= 0.f)
			return result;

		InitializeFromActorIfNeeded(actor);

		const PxTransform pose = actor->getGlobalPose();

		// actor pose가 외부에서 수정됐을 가능성에 맞춰 동기화
		m_state.position = ToPx(pose.p);

		Vec3 disp = Vec3::Zero();
		IntegrateMotion(dt, scene->getGravity(), disp);

		const float dist = disp.Magnitude();
		if (dist < EPSILON)
		{
			// 움직임이 거의 없어도 lifetime은 흐름
			CheckLifetime(result);
			return result;
		}

		if (QueryHit(scene, actor, pose, disp, teamId, result))
			return result;

		// 이동 적용
		PxTransform newPose = pose;
		newPose.p += ToPhysX(disp);
		actor->setKinematicTarget(newPose);

		m_state.position += disp;
		m_state.traveledDist += dist;

		CheckLifetime(result);
		return result;
	}

} // namespace jam::px
