#pragma once

#include "jampx/PhysicsQueryFilter.h"
#include "jampx/actor/character/CharacterMovementTypes.h"

namespace physx
{
	class PxCapsuleController;
	class PxRigidDynamic;
}

namespace jam::px
{
    // ---- Motor sensing snapshot (fed into core) ----
    struct MotorSense
    {
        bool					grounded = false;
        bool					ceiling  = false;

        // Optional: ground normal, slope info, etc.
        Vec3					groundNormal{ 0,1,0 };

        MoveCollision::Flags	collisionFlags = MoveCollision::NONE;
    };

    // ---- Motor step result ----
    struct MotorStepResult
    {
        Vec3                    position = Vec3::Zero();
        Vec3                    velocity = Vec3::Zero();

        MotorSense              sense{};
    };


	class CharacterMotor
	{
    public:
        explicit CharacterMotor(PxCapsuleController* controller, PxRigidActor* hitbox = nullptr, bool collideWithControllers = true);
        ~CharacterMotor();

        /// @brief Read sensing info before update(grounded/ceiling/flags/etc.)
        MotorSense              Sense() const;

        /// @brief Apply resize request (only height)
        bool                    TryResize(float height);

        /// @brief Resoleve collision for desired displacement over dt (CCT move expects displacement)
        MotorStepResult         Move(float dt, const Vec3& displacement);

        void                    Teleport(const Vec3& p);

        /// @brief SetMoveState 이후 sense 캐시를 강제 동기화 (Teleport/Rewind 후 stale 방지)
        void                    OverrideSense(bool grounded, bool ceiling = false);

        Vec3                    GetPosition() const;
        float                   GetRadius() const;
        float                   GetHeight() const;

    private:
        void                    RefreshSenseCache() const;
        void                    FollowHitboxToController();

    private:
        PxCapsuleController*                            m_controller    = nullptr;
        PxRigidDynamic*                                 m_hitbox        = nullptr;

        RequestQueryFD                                  m_rqfd{};
        std::unique_ptr<PxQueryFilterCallback>          m_qryCallback   = nullptr;
        std::unique_ptr<PxControllerFilterCallback>     m_cctCallback   = nullptr;

        mutable MotorSense                              m_lastSense{};
        mutable bool                                    m_lastSenseValid = false;
	};
}
