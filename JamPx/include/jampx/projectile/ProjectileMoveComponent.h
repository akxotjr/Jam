#pragma once


namespace jam::px
{
	struct ProjectileMoveConfig
	{
		float		gravityScale	= 1.f;
		float		maxRange		= 1000.f;
	};

	struct ProjectileMoveState
	{
		Vec3		velocity		= Vec3::Zero();
		float		traveledDist	= 0.f;
	};

	struct ProjectileHitResult
	{
		bool		hit				= false;
		bool		maxRangeReached = false;
		Vec3		position		= Vec3::Zero();
		Vec3		normal			= Vec3::Zero();
		ObjectId	hitId			= INVALID_OBJ_ID;

		bool IsTerminal() const { return hit || maxRangeReached; }
	};

	/// @brief Anaytic Projectile component
	class ProjectileMoveComponent
	{
	public:
		explicit ProjectileMoveComponent(const ProjectileMoveConfig& cfg, const Vec3& initialVel);

		ProjectileHitResult			Tick(float dt, PxScene* scene, PxRigidDynamic* actor, uint16 teamId);

		const ProjectileMoveState&	GetState() const { return m_state; }
		void						SetState(const ProjectileMoveState& state) { m_state = state; }

	private:
		ProjectileMoveConfig		m_config{};
		ProjectileMoveState			m_state{};
	};

}
