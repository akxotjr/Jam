#pragma once
#include "jamnet/sync/replication/ReplicationTypes.h"
#include "jamnet/sync/replication/IReplayRunner.h"

namespace jam::px
{
    class IPhysicsFacade;
}

namespace jam::net
{

	class ClientPhysicsSystem
    {
    public:
        ClientPhysicsSystem(entt::registry& world, px::IPhysicsFacade* physics);
        ~ClientPhysicsSystem() = default;

        void                                Init();
        void                                Tick();

		void                                SpawnActor(entt::entity e, bool isLocal) const;
        void                                DespawnActor(entt::entity e) const;

        void                                SetReconcileConfig(const ReconcileConfig& config) { m_config = config; }
        const ReconcileConfig&              GetReconcileConfig() const { return m_config; }

    private:
        void                                LivePredict();

        void                                Reconcile();
        void                                PushAuthorityStates();
        void                                PullProxyStates();

        void                                Rewind(const ReplayContext& ctx);
        void                                Replay(const ReplayContext& ctx);
        void                                ApplyInput(const InputCmd& cmd);

        void                                Simulate();
        void                                Resimulate();
        void                                HandleProjectileLifecycleEvents();

        void                                CommitPendingActorOps() const;

	private:

        struct PendingActorOp
        {
            enum class eType
            {
                Spawn,
                Despawn,
            };

            eType           type{};
            entt::entity    e{ entt::null };
            bool            isLocal = false;
            bool            isRigid = false;
        };

    private:
        entt::registry&                     m_world;
        px::IPhysicsFacade*                 m_physics           = nullptr;
        std::unique_ptr<IReplayRunner>      m_replayRunner      = nullptr;

        uint64                              m_userId            = 0;
        ReconcileConfig                     m_config            = {};

        bool                                m_tickFiberRunning  = false;
        uint64                              m_awaitSeq          = 0;

        mutable std::vector<PendingActorOp> m_pendingActorOps;
    };
}

