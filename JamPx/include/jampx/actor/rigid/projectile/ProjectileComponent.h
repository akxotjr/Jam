#pragma once


namespace jam::px
{
	enum class eProjectileMotionModel : uint8
	{
		Linear,
		Ballistic,
		Homing
	};

	enum class eProjectileHitModel : uint8
	{
		RaycastFallback,   // shape 없으면 raycast
		ShapeSweep,        // actor shape 기반 sweep, 실패 시 fallback 가능
		SphereSweep,	   
		ExpandingShapeSweep,
		ExpandingSphereSweep,
	};


	struct ProjectileMotionConfig
	{
		eProjectileMotionModel	model			= eProjectileMotionModel::Ballistic;
		Vec3					initialVelocity = Vec3::Zero();
		float					gravityScale	= 1.f;  // scene gravity 사용 기준.
	};

	struct ProjectileHitConfig
	{
		eProjectileHitModel	model = eProjectileHitModel::ShapeSweep;

		bool				useShapeSweep = true;
		bool				fallbackRaycast = true;

		RequestQueryFD		requestFd = MakeRequestQueryFD(
			QueryCategory::WORLD | QueryCategory::CHARACTER | QueryCategory::HITBOX,
			0,
			QuerySublayer::Default,
			0,
			0, 0, 0,
			RequestQueryFlag::IGNORE_TRIGGERS | RequestQueryFlag::IGNORE_SAME_TEAM);
	};

	struct ProjectileLifetimeConfig
	{
		float				maxRange = 1000.f;
		float				maxLifetime = 10.f;
	};

	struct ProjectileConfig
	{
		ProjectileMotionConfig		motion	 = {};
		ProjectileHitConfig			hit		 = {};
		ProjectileLifetimeConfig	lifetime = {};
	};

	struct ProjectileState
	{
		Vec3		position			= Vec3::Zero();
		Vec3		velocity			= Vec3::Zero();
		float		age					= 0.f;
		float		traveledDist		= 0.f;
		bool		started				= false;
	};

	struct ProjectileHitResult
	{
		bool		hit					= false;
		bool		maxRangeReached		= false;
		bool		maxLifetimeReached	= false;

		Vec3		position			= Vec3::Zero();
		Vec3		normal				= Vec3::Zero();
		ObjectId	hitId				= INVALID_OBJ_ID;

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

		ProjectileHitResult			Tick(float dt, const PxScene* scene, PxRigidDynamic* actor);

		const ProjectileConfig&		GetConfig() const { return m_config; }
		const ProjectileState&		GetState() const { return m_state; }
		void						SetState(const ProjectileState& state) { m_state = state; }

		const RequestQueryFD&		GetRequestFd() const { return m_config.hit.requestFd; }
		RequestQueryFD&				EditRequestFd() { return m_config.hit.requestFd; }
		void						SetRequestFd(const RequestQueryFD& fd) { m_config.hit.requestFd = fd; }

		RequestQueryFD				BuildRuntimeRequestFd(uint16 teamId, const PxRigidActor* selfActor) const;

	private:
		void						IntegrateMotion(float dt, const PxVec3& sceneGravity, OUT Vec3& outDisp);
		bool						CheckLifetime(OUT ProjectileHitResult& outResult) const;

		bool						QueryHit(
										const PxScene* scene,
										const PxRigidDynamic* actor,
										const PxTransform& pose,
										const Vec3& disp,
										OUT ProjectileHitResult& result) const;

	private:
		ProjectileConfig			m_config	= {};
		ProjectileState				m_state		= {};
	};

} // namespace jam::px
