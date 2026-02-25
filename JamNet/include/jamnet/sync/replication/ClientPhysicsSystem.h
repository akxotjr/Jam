#pragma once
#include "NetActorComponents.h"
#include "ReplicationTypes.h"


namespace jam::px
{
    class IPhysicsFacade;
}

namespace jam::net
{
	/**
	 * @class ClientPhysicsSystem
	 * @brief Physics(Prediction, Reconciliation)
	 * @details 
	 *  - Local Actor Prediction (현재 입력 -> Simulate)
	 *  - Local Actor Reconiliation (서버 상태 -> Rewind -> Replay)
	 *  - PhysX Actor 생성 관리
	 *  - Transform 동기화
	 */
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

		const px::Vec3&                     GetVisualOffset() const { return m_visualPosOffset; }

    private:

        void                                CheckAndReconcile();
        void                                PredictCurrentFrame();
        void                                SyncTransforms();

        void                                Reconcile(const ServerState& serverState);
        void                                RewindToServerState(const ServerState& serverState);
        void                                ReplayInputs(uint32 fromSeq);

        void                                ApplyInput(const InputCmd& cmd);

        void                                Simulate();

        void                                SavePredictedState(uint32 inputSeq);
        optional<PredictedState>            GetPredictedState(uint32 inputSeq) const;
        void                                PrunePredictedHistory(uint32 upToSeq);

        px::CharacterState*                 GetLocalCharacterState() const;


        float                               CalculateRotationError(const px::Quat& a, const px::Quat& b) const;

    private:
        entt::registry&                     m_world;
        px::IPhysicsFacade*                 m_physics;

        uint64                              m_userId = 0;
        deque<PredictedState>               m_predictedHistory;

        ReconcileConfig                     m_config{};

        uint64                              m_awaitSeq = 0;

        uint32                              m_lastReconciledSeq = 0;
        uint32                              m_lastReconciledServerTick = 0;

		px::Vec3                            m_visualPosOffset = px::Vec3::Zero();

        static constexpr size_t             kMaxPredictedHistroySize = 128;
    };
}

