#pragma once


namespace jam::px
{
	enum class eProjectileKind : uint8
	{
		DYN_SIM,		// 0~30   m/s : PxRigidDynamic, PhysX simulate
		ANALYTIC,		// 30-300 m/s : Kinematic + Manual integration + Sweep CCD
		HITSCAN,		// 300+   m/s : Instant raycast
	};

	enum class eProjectileMotionModel : uint8
	{
		Linear,
		Ballistic,
		HomingSteer,
		HomingLead,
		HomingPN,
	};

	enum class eProjectileHitModel : uint8
	{
		RaycastFallback,   // shape 없으면 raycast
		ShapeSweep,        // actor shape 기반 sweep, 실패 시 fallback 가능
		SphereSweep,	   
		ExpandingShapeSweep,
		ExpandingSphereSweep,
	};


	struct ProjectileHomingConfig
	{
		ObjectId	targetId			= INVALID_OBJ_ID;

		float		maxSpeed			= 120.f;
		float		acceleration		= 300.f;
		PxReal		maxTurnRate			= 6.0f;   // rad/s

		bool		enableHoming		= false;
		bool		keepSpeedConstant	= true;
		bool		reacquireTarget		= false;
		bool		keepLastDirection	= true;


		// Lead pursuit
		PxReal		leadTimeScale		= 1.0f;
		PxReal		maxPredictTime		= 0.35f;

		// Proportional Navigation
		PxReal		navigationGain		= 3.0f;   // N
		PxReal		maxLateralAccel		= 200.0f; // m/s^2
	};

	struct ProjectileHomingTarget
	{
		bool		valid				= false;
		ObjectId	targetId			= INVALID_OBJ_ID;
		PxVec3		position			= PxVec3(physx::PxZero);
		PxVec3		velocity			= PxVec3(physx::PxZero);
	};

	struct ProjectileMotionConfig
	{
		eProjectileMotionModel	model			= eProjectileMotionModel::Ballistic;
		PxVec3					initialVelocity = PxVec3(physx::PxZero);
		float					gravityScale	= 1.f;  // scene gravity 사용 기준.
	};

	struct ProjectileHitConfig
	{
		eProjectileHitModel	model			= eProjectileHitModel::ShapeSweep;

		bool				useShapeSweep	= true;
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
		float				maxRange	= 1000.f;
		float				maxLifetime = 10.f;
	};

	struct ProjectileConfig
	{
		eProjectileKind				kind = eProjectileKind::ANALYTIC;

		ProjectileMotionConfig		motion	 = {};
		ProjectileHitConfig			hit		 = {};
		ProjectileLifetimeConfig	lifetime = {};

		ProjectileHomingConfig		homing	 = {};
	};

	struct ProjectileState
	{
		PxVec3		position			= PxVec3(physx::PxZero);
		PxVec3		velocity			= PxVec3(physx::PxZero);
		float		age					= 0.f;
		float		traveledDist		= 0.f;
		bool		started				= false;
	};

	struct ProjectileHitResult
	{
		bool		hit					= false;
		bool		maxRangeReached		= false;
		bool		maxLifetimeReached	= false;

		PxVec3		position			= PxVec3(physx::PxZero);
		PxVec3		normal				= PxVec3(physx::PxZero);
		ObjectId	hitId				= INVALID_OBJ_ID;

		bool IsTerminal() const
		{
			return hit || maxRangeReached || maxLifetimeReached;
		}
	};

	using ProjectileTargetResolver = std::function<bool(ObjectId, ProjectileHomingTarget&)>;

	/// @brief analytic / kinematic projectile simulation component
	class ProjectileComponent
	{
	public:
		explicit ProjectileComponent(const ProjectileConfig& cfg);

		ProjectileHitResult			Tick(float dt, const PxScene* scene, PxRigidDynamic* actor);

		const ProjectileConfig&		GetConfig() const { return m_config; }
		const ProjectileState&		GetState() const { return m_state; }
		void						SetState(const ProjectileState& state) { m_state = state; }

		void						SetTargetResolver(ProjectileTargetResolver resolver) { m_reolver = std::move(resolver); }

		const RequestQueryFD&		GetRequestFd() const { return m_config.hit.requestFd; }
		RequestQueryFD&				EditRequestFd() { return m_config.hit.requestFd; }
		void						SetRequestFd(const RequestQueryFD& fd) { m_config.hit.requestFd = fd; }

	private:
		void						IntegrateMotion(float dt, const PxVec3& sceneGravity, OUT PxVec3& disp);
		
		void						IntegrateHomingSteer(float dt, OUT PxVec3& disp);
		void						IntegrateHomingLead(float dt, OUT PxVec3& disp);
		void						IntegrateHomingPN(float dt, OUT PxVec3& disp);
		
		bool						ResolveHomingTarget(OUT ProjectileHomingTarget& target) const;

		bool						CheckLifetime(OUT ProjectileHitResult& result) const;

		bool						QueryHit(
										const PxScene* scene,
										const PxRigidDynamic* actor,
										const PxTransform& pose,
										const PxVec3& disp,
										OUT ProjectileHitResult& result) const;

	private:
		ProjectileConfig			m_config	= {};
		ProjectileState				m_state		= {};

		ProjectileTargetResolver	m_reolver	= nullptr;
	};

} // namespace jam::px
