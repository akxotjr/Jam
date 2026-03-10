#pragma once


#include "jampx/character/CharacterMovementTypes.h"
#include "jampx/character/IAccelerator.h"
#include "jampx/character/CharacterMotor.h"
#include "jampx/character/ExternalMoveAccumulator.h"

namespace jam::px
{
    class LocomotionComponent
    {
    public:
        explicit LocomotionComponent(const CharacterMoveConfig& cfg, PxCapsuleController* controller, PxRigidActor* hitbox);

        void                            SetConfig(const CharacterMoveConfig& cfg) { m_cfg = cfg; }
        const CharacterMoveConfig&      GetConfig() const { return m_cfg; }

        void                            SetMoveState(const CharacterMoveState& state);
        const CharacterMoveState&       GetMoveState() const { return m_state; }

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
        CharacterMoveConfig                         m_cfg{};
        CharacterMoveState                          m_state{};

        std::unique_ptr<IAccelerator>               m_accelerator = nullptr;
        std::unique_ptr<ExternalMoveAccumulator>    m_accumulator = nullptr;
        std::unique_ptr<CharacterMotor>             m_motor       = nullptr;
    };
}
