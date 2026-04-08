#pragma once

#include <jampx/IPhysicsFacade.h>

namespace jam::net
{
	class ServerPhysicsSystem
	{
    public:
        ServerPhysicsSystem(entt::registry& world, px::IPhysicsFacade* physics);

        void                    Init() const;
        void                    Tick();

        void                    SpawnActor(entt::entity e, const px::SpawnDesc& desc) const;
        void                    DespawnActor(entt::entity e) const;

    private:
        void                    ApplyInputs() const;
        void                    Simulate();

        void                    SyncActiveTransforms() const;
        void                    SyncTransforms() const;
        void                    HandleProjectileLifecycleEvents() const;

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
            px::eBodyType   bodyType = px::eBodyType::None;
        };

    private:
        entt::registry&                     m_world;
        px::IPhysicsFacade*                 m_physics           = nullptr;

        uint64                              m_awaitSeq          = 0;
        bool                                m_tickFiberRunning  = false;

        mutable std::vector<PendingActorOp> m_pendingActorOps;
	};
}
