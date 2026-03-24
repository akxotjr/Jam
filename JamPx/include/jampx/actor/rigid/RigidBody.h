#pragma once

#include "jampx/actor/rigid/IRigidBehavior.h"


namespace jam::px
{

    class RigidBody
    {
    public:
        explicit RigidBody(PxRigidActor* actor, PxRigidActor* replayActor = nullptr, std::unique_ptr<IRigidBehavior> behavior = nullptr);

        void                                AttachBehavior(std::unique_ptr<IRigidBehavior> behavior) { m_behavior = std::move(behavior); }
        void                                DetachBehavior() { m_behavior.reset(); }
        IRigidBehavior*                     GetBehavior() const { return m_behavior.get(); }

        void                                TickOnMain(float dt);
        void                                TickOnReplay(float dt);
        bool                                SyncMainState(RigidBody& body);
        void                                SyncReplayState(RigidBody& body);

        RigidState                          GetMainState() const;
        RigidState                          GetReplayState() const;

        void                                SetMainState(const RigidState& state)   { m_mainState = state; }
        void                                SetReplayState(const RigidState& state) { m_replayState = state; }

        void                                ApplyMainState(const RigidState& s, bool kinematicLike);
        void                                ApplyReplayState(const RigidState& s, bool kinematicLike);
        void                                ApplyStateBoth(const RigidState& s, bool kinematicLike);

        PxRigidActor*                       GetMainActor()   const { return m_mainActor; }
        PxRigidActor*                       GetReplayActor() const { return m_replayActor; }
        bool                                HasMainActor()   const { return m_mainActor != nullptr; }
        bool                                HasReplayActor() const { return m_replayActor != nullptr; }

        eActorType                          GetActorType() const { return m_behavior ? m_behavior->GetActorType() : eActorType::None; }
        eBodyType                           GetBodyType()  const { return eBodyType::Rigid; }

    private:
        PxRigidActor*                       m_mainActor     = nullptr;  // in main scene
        PxRigidActor*                       m_replayActor   = nullptr;  // in replay scene

        RigidState                          m_mainState     = {};
        RigidState                          m_replayState   = {};

        std::unique_ptr<IRigidBehavior>     m_behavior      = nullptr;
    };

} // namespace jam::px
