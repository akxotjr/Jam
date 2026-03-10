#pragma once

#include "jampx/kinematic/KinematicDriverComponent.h"

namespace jam::px
{


    class RigidBody
    {
    public:
        RigidBody(PxRigidActor* actor);

        // --- kinematic driver (optional) ---
        void                            AttachDriver(std::unique_ptr<IKinematicDriver> driver);
        void                            DetachDriver();
        KinematicDriverComponent*       GetDriver() { return m_driver ? m_driver.get() : nullptr; }

        void                            Tick(float dt);

        // --- state ---
        const RigidState&               GetState()  const { return m_state; }
        void                            SetState(const RigidState& s) { m_state = s; }

        // --- accessors ---
        PhysicsHandle                   GetPhysicsHandle();
        PxRigidActor*                   GetActor()          const { return m_actor; }
        eBodyType                       GetBodyType() const { return eBodyType::Rigid; }

    private:
        PxRigidActor*                               m_actor  = nullptr;  // non-owning

        RigidState                                  m_state  = {};
        std::unique_ptr<KinematicDriverComponent>   m_driver = nullptr;
    };
}
