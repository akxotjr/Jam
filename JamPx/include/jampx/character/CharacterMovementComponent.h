#pragma once

#include <jampx/api/PhysicsTypes.h>

#include "jampx/character/IAccelerator.h"
#include "jampx/character/CharacterMotor.h"
#include "jampx/character/ExternalMoveAccumulator.h"

namespace jam::px
{
    class CharacterMovementComponent
    {
    public:
        explicit CharacterMovementComponent(const MovementConfig& cfg, PxCapsuleController* controller, PxRigidActor* hitbox);

        void                            SetConfig(const MovementConfig& cfg) { m_cfg = cfg; }
        const MovementConfig&           GetConfig() const { return m_cfg; }

        void                            SetMoveState(const MovementState& state);
        const MovementState&            GetMoveState() const { return m_state; }

        void                            Tick(float dt, const MoveIntent& intent);


        void                            GetCharacterState(CharacterState& state) const;

        void                            Teleport(const Vec3& pos);

    private:
        void                            UpdateJumpTimers(float dt);
        
    	void                            ApplyStanceRequest(const MoveIntent& intent);
        void                            ApplyGaitRequest(const MoveIntent& intent);
        void                            ApplyJump(const MoveIntent& intent);
        void                            ApplyGravity(float dt);
        void                            ApplyDash(float dt, const MoveIntent& intent);

        void                            PostMoveUpdate(const MotorSense& sense);

        bool                            CanSprint();

    private:
        MovementConfig                              m_cfg{};
        MovementState                               m_state{};

        std::unique_ptr<IAccelerator>               m_accelerator = nullptr;
        std::unique_ptr<ExternalMoveAccumulator>    m_accumulator = nullptr;
        std::unique_ptr<CharacterMotor>             m_motor       = nullptr;
    };
}
