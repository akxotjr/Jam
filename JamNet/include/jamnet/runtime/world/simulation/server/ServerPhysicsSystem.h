#pragma once

#include <jampx/PhysicsTypes.h>

namespace jam::px
{
	class PhysicsFacade;
}

namespace jam::net
{
	class ServerPhysicsSystem
	{
    public:
        ServerPhysicsSystem(entt::registry& world, px::PhysicsFacade* physics);

        void                    Init();
        void                    Tick();

        uint32                  GetCompletedTickCount() const { return m_completedTickCount; }
        bool                    IsTickFiberRunning() const { return m_tickFiberRunning; }

        void                    SpawnActor(entt::entity e, const px::SpawnDesc& desc) const;
        void                    DespawnActor(entt::entity e) const;
        const std::vector<entt::entity>& GetLastActiveEntities() const { return m_lastActiveEntities; }

    private:
        void                    ApplyInputs() const;
        void                    Simulate();

        void                    SyncActiveTransforms() const;
        void                    SyncTransforms() const;
        void                    HandlePhysicsEvents() const;

        void					CommitPendingActorOps();

    private:
        struct PendingActorOp
        {
            enum class eType
            {
                Spawn,
                Despawn,
            };

            eType           type     = eType::Spawn;
            entt::entity    e        = entt::null;
        };

    private:
        entt::registry&                     m_world;
        px::PhysicsFacade*                  m_physics           = nullptr;

        bool                                m_tickFiberRunning  = false;
        uint32                              m_completedTickCount = 0;

        mutable std::vector<PendingActorOp> m_pendingActorOps;
        mutable std::vector<entt::entity>   m_lastActiveEntities;
	};
}
