#include "pch.h"
#include "jampx/character/DefaultQuakeAccelerator.h"



namespace jam::px
{
	namespace 
	{

        static void ApplyGroundFriction(CharacterMoveState& st, const CharacterMoveConfig& cfg, float dt)
        {
            const float speed = HorizontalSpeed(st.velocity);
            if (speed <= EPSILON) return;

            const float drop     = speed * cfg.groundFriction * dt;
            const float newSpeed = std::max(0.0f, speed - drop);
            const float ratio    = newSpeed / speed;

            st.velocity.x *= ratio;
            st.velocity.z *= ratio;
        }

        // Quake-style accelerate: wishDir 방향 성분만 add
        static void Accelerate(CharacterMoveState& st, const Vec3& wishDir, float wishSpeed, float accel, float dt)
        {
            if (wishSpeed <= 0.0f) return;
            if (wishDir.MagnitudeSquared() <= EPSILON) return;

            Vec3 v = st.velocity;
            float currentSpeed  = v.Dot(wishDir);
            float addSpeed      = wishSpeed - currentSpeed;
            if (addSpeed <= 0.0f) return;

            float accelSpeed = accel * dt * wishSpeed;
            accelSpeed = std::min(accelSpeed, addSpeed);

            st.velocity += wishDir * accelSpeed;
        }

       
        static void ApplyAirSpeedCap(CharacterMoveState& st, const CharacterMoveConfig& cfg, float dt)
        {
            Vec3 v      = st.velocity;
            float speed = cfg.capHorizontalOnly ? HorizontalSpeed(v) : v.Magnitude();
            if (speed <= EPSILON) return;

            // soft cap
            if (cfg.softCapStartAir > 0.0f && cfg.softCapStrengthAir > 0.0f && speed > cfg.softCapStartAir)
            {
                const float excess = speed - cfg.softCapStartAir;
                const float cut    = excess * cfg.softCapStrengthAir * dt;

                const float newSpeed = std::max(cfg.softCapStartAir, speed - cut);
                const float ratio    = newSpeed / speed;

                if (cfg.capHorizontalOnly)
                {
                    st.velocity.x *= ratio;
                    st.velocity.z *= ratio;
                }
                else
                {
                    st.velocity *= ratio;
                }
            }

            // hard cap
            if (cfg.hardSpeedCapAir > 0.0f)
            {
                speed  = cfg.capHorizontalOnly ? HorizontalSpeed(st.velocity) : st.velocity.Magnitude();

                if (speed > cfg.hardSpeedCapAir)
                {
                    const float ratio = cfg.hardSpeedCapAir / speed;
                    if (cfg.capHorizontalOnly)
                    {
                        st.velocity.x *= ratio;
                        st.velocity.z *= ratio;
                    }
                    else
                    {
                        st.velocity *= ratio;
                    }
                }
            }
        }
	}


	DefaultQuakeAccelerator::DefaultQuakeAccelerator(const CharacterMoveConfig& cfg)
		: m_cfg(cfg)
	{
	}

	WishMovement DefaultQuakeAccelerator::BuildWishMovement(const MoveIntent& intent) const
	{
        WishMovement w{};

        // AI path: world-space dir 이 직접 제공된 경우 local 변환 스킵
        if (intent.wishDir.has_value())
        {
	        w.dir   = intent.wishDir.value().GetNormalized();
            w.speed = Saturate(intent.moveMag);

            return w;
        }

        // Player path: local 축 -> world 변환
        // moveX=right, moveY=forward (local)
        Vec2 local(intent.moveX, intent.moveY);
        const float axisMag = std::min(1.0f, local.Magnitude());
        local = local.GetNormalized();

        const float cy = std::cos(intent.moveYaw);
        const float sy = std::sin(intent.moveYaw);

        const Vec3 forward{ sy, 0.0f, cy };
        const Vec3 right{ cy, 0.0f, -sy };

        const Vec3 wishVel = forward * local.y + right * local.x;

        w.dir   = wishVel.GetNormalized();
        w.speed = Saturate(axisMag * Saturate(intent.moveMag)); // 0..1

        return w;
	}

	void DefaultQuakeAccelerator::Integrate(CharacterMoveState& st, const WishMovement& wish, float dt) const
	{
        if (dt <= 0.0f) return;

        // ground friction
        if (st.grounded)
            ApplyGroundFriction(st, m_cfg, dt);

        // accel
        const float accelBase = st.grounded ? m_cfg.groundAccel : m_cfg.airAccel;
        const float accel     = accelBase * GetGaitAccelMultiplier(m_cfg.gait, st.gait);

        // max speed (stance/gait)
        const float baseMax  = st.grounded ? m_cfg.groundMaxSpeed : m_cfg.airMaxSpeed;
        const float maxSpeed = baseMax * GetStanceSpeedMultiplier(m_cfg.stance, st.stance) * GetGaitSpeedMultiplier(m_cfg.gait, st.gait);

        const float wishSpeed = maxSpeed * wish.speed;
        Accelerate(st, wish.dir, wishSpeed, accel, dt);

        // air cap only
        if (!st.grounded)
            ApplyAirSpeedCap(st, m_cfg, dt);
	}
}
