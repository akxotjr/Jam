#pragma once

#include "jampx/character/LocomotionComponent.h"
#include "jampx/character/ICharacterController.h"


namespace jam::px
{
	class CharacterBody
	{
    public:
        CharacterBody(PxCapsuleController* controller, PxRigidActor* hitbox, const CharacterMoveConfig& cfg);

        // --- brain (Player / AI) ---
        void                            SetBrain(std::unique_ptr<ICharacterController> brain);
        ICharacterController*           GetBrain() { return m_brain.get(); }

        // --- tick ---
        void                            Tick(float dt);

        // --- state / config ---
        const CharacterState&           GetState() const { return m_state; }
        void                            SetState(const CharacterState& s);
        void                            SetFacing(float yaw, float pitch);
        const CharacterMoveConfig&      GetConfig() const;
        void                            SetConfig(const CharacterMoveConfig& cfg);

        // --- accessors ---
        PhysicsHandle                   GetPhysicsHandle() const;
        PxCapsuleController*            GetController() const { return m_controller; }
        PxRigidActor*                   GetHitbox() const { return m_hitbox; }
        LocomotionComponent*            GetMover() { return m_mover.get(); }
        eBodyType                       GetBodyType() const { return eBodyType::Rigid; }

    private:
        PxCapsuleController*                    m_controller        = nullptr;   // non-owning
        PxRigidActor*                           m_hitbox            = nullptr;   // non-owning

        CharacterState                          m_state             = {};
        std::unique_ptr<LocomotionComponent>    m_mover             = nullptr;
        std::unique_ptr<ICharacterController>   m_brain             = nullptr;
	};


} // namespace jam::px
