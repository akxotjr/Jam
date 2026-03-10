#pragma once

#include "jampx/actor/rigid/IRigidBehavior.h"


namespace jam::px
{

    class RigidBody
    {
    public:
        explicit RigidBody(PxRigidActor* actor, std::unique_ptr<IRigidBehavior> behavior = nullptr);

        void                                AttachBehavior(std::unique_ptr<IRigidBehavior> behavior) { m_behavior = std::move(behavior); }
        void                                DetachBehavior() { m_behavior.reset(); }
        IRigidBehavior*                     GetBehavior() const { return m_behavior.get(); }

        void                                Tick(float dt);

        // --- state ---
        const RigidState&                   GetState() const { return m_state; }
        void                                SetState(const RigidState& s) { m_state = s; }

        // --- accessors ---
        PhysicsHandle                       GetPhysicsHandle();
        PxRigidActor*                       GetActor() const { return m_actor; }
        eActorType                          GetActorType() const { return m_behavior ? m_behavior->GetActorType() : eActorType::None; }
        eBodyType                           GetBodyType() const { return eBodyType::Rigid; }

    private:
        PxRigidActor*                       m_actor    = nullptr;  // non-owning
        RigidState                          m_state    = {};
        std::unique_ptr<IRigidBehavior>     m_behavior = nullptr;
    };

} // namespace jam::px
