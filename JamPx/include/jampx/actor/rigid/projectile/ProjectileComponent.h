#pragma once


namespace jam::px
{
	enum class eProjectileMotionModel : uint8_t
	{
		Linear,
		Ballistic,
		// Homing, // 나중에 확장
	};

	enum class eProjectileHitModel : uint8_t
	{
		RaycastFallback,   // shape 없으면 raycast
		ShapeSweep,        // actor shape 기반 sweep, 실패 시 fallback 가능
		// SphereSweep,     // 나중에 확장
	};

	struct ProjectileMotionConfig
	{
		eProjectileMotionModel	model = eProjectileMotionModel::Ballistic;
		Vec3					initialVelocity = Vec3::Zero();

		// scene gravity 사용 기준. Y-up 전제여도 벡터로 들고 있으면 확장하기 편함.
		float					gravityScale = 1.f;
	};

	struct ProjectileHitConfig
	{
		eProjectileHitModel	model = eProjectileHitModel::ShapeSweep;

		bool				useShapeSweep = true;
		bool				fallbackRaycast = true;

		bool				ignoreTriggers = true;
		bool				ignoreSameTeam = true;

		QueryCategory::Flags queryMask =
			QueryCategory::WORLD | QueryCategory::CHARACTER | QueryCategory::HITBOX;
	};

	struct ProjectileLifetimeConfig
	{
		float				maxRange = 1000.f;
		float				maxLifetime = 10.f;
	};

	struct ProjectileConfig
	{
		ProjectileMotionConfig	motion = {};
		ProjectileHitConfig		hit = {};
		ProjectileLifetimeConfig	lifetime = {};
	};

	struct ProjectileState
	{
		Vec3		position = Vec3::Zero();
		Vec3		velocity = Vec3::Zero();
		float		age = 0.f;
		float		traveledDist = 0.f;
		bool		started = false;
	};

	struct ProjectileHitResult
	{
		bool		hit = false;
		bool		maxRangeReached = false;
		bool		maxLifetimeReached = false;

		Vec3		position = Vec3::Zero();
		Vec3		normal = Vec3::Zero();
		ObjectId	hitId = INVALID_OBJ_ID;

		bool IsTerminal() const
		{
			return hit || maxRangeReached || maxLifetimeReached;
		}
	};

	/// @brief analytic / kinematic projectile simulation component
	class ProjectileComponent
	{
	public:
		explicit ProjectileComponent(const ProjectileConfig& cfg);

		ProjectileHitResult Tick(float dt, PxScene* scene, PxRigidDynamic* actor, uint16 teamId);

		const ProjectileConfig& GetConfig() const { return m_config; }

		const ProjectileState& GetState() const { return m_state; }
		void SetState(const ProjectileState& state) { m_state = state; }

	private:
		void InitializeFromActorIfNeeded(PxRigidDynamic* actor);
		void IntegrateMotion(float dt, const PxVec3& sceneGravity, Vec3& outDisp);
		bool CheckLifetime(ProjectileHitResult& outResult) const;

		PxQueryFilterData BuildQueryFilterData(uint16 teamId) const;
		bool QueryHit(
			PxScene* scene,
			PxRigidDynamic* actor,
			const PxTransform& pose,
			const Vec3& disp,
			uint16 teamId,
			ProjectileHitResult& outResult) const;

	private:
		ProjectileConfig	m_config = {};
		ProjectileState		m_state = {};
	};

} // namespace jam::px
