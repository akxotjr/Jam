#pragma once
#include "jamnet/runtime/world/simulation/common/ReplicationTypes.h"
#include "jamnet/runtime/world/simulation/common/IReplayRunner.h"
#include "jamnet/runtime/world/simulation/common/CharacterControlResolver.h"

namespace jam::px
{
    class PhysicsFacade;
}

namespace jam::net
{

	class ClientPhysicsSystem
    {
    public:
        ClientPhysicsSystem(entt::registry& world, px::PhysicsFacade* physics);
        ~ClientPhysicsSystem() = default;

        void                                Init();
        void                                Tick();

        uint32                              GetCompletedTickCount() const { return m_completedTickCount; }
        bool                                IsTickFiberRunning() const { return m_tickFiberRunning; }

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
		void                                ApplyInput(const CharacterControlCommand& cmd, bool useReplayState = false);
		px::CharacterMotorInput                  ResolveInputForSimulation(const CharacterControlIntent& intent, const px::CharacterState* selfState, bool useReplayState) const;
        bool                                TryResolveTargetPos(uint32 targetActorIdRaw, OUT px::Vec3& outPos, bool useReplayState) const;

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

            eType           type    = eType::Spawn;
            entt::entity    e       = entt::null;
            bool            isLocal = false;
            bool            isRigid = false;
        };

    private:
        entt::registry&                     m_world;
        px::PhysicsFacade*                  m_physics           = nullptr;
        std::unique_ptr<IReplayRunner>      m_replayRunner      = nullptr;

        uint64                              m_userId            = 0;
        ReconcileConfig                     m_config            = {};
		CharacterControlResolveConfig		m_controlResolveConfig = {};

        bool                                m_tickFiberRunning  = false;

        uint32                              m_completedTickCount = 0;

        std::vector<px::ActorContext>       m_pushContexts;
        std::vector<px::ActorContext>       m_pullContexts;

        mutable std::vector<PendingActorOp> m_pendingActorOps;
    };
}
