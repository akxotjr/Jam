#include "pch.h"
#include "jamnet/sync/replication/ClientPhysicsSystem.h"
#include "jamnet/sync/replication/NetActorComponents.h"
#include "jamnet/sync/replication/ClientInputSystem.h"
#include "jamnet/sync/replication/ClientReplicationSystem.h"
#include "jamnet/sync/networld/ClientNetWorld.h"

#include <PhysicsTypes.h>
#include <IPhysicsFacade.h>

#include "jamnet/sync/replication/NetWorldContext.h"


namespace jam::net
{

	ClientPhysicsSystem::ClientPhysicsSystem(entt::registry& world, px::IPhysicsFacade* physics)
		: m_world(world), m_physics(physics)
	{
	}

    void ClientPhysicsSystem::Init()
	{
        m_predictedHistory.clear();
        m_lastReconciledSeq = 0;

        if (auto* nw = m_world.ctx().find<ClientNetWorld*>())
            m_userId = (*nw) ? (*nw)->GetUserId() : 0;
    }

    void ClientPhysicsSystem::Tick()
    {
        CheckAndReconcile();
        PredictCurrentFrame();
        SyncTransforms();
    }

    void ClientPhysicsSystem::SpawnActor(entt::entity e, bool isLocal) const
    {
        if (!m_physics || !m_world.valid(e)) return;

        const auto* pk = m_world.try_get<NetPrefabKey>(e);
        if (!pk || !pk->key.IsValid()) return;

        const px::ObjectKey key = MakeObjectKey(e);

        px::SpawnDesc desc{};
        desc.prefab      = pk->key;
        desc.isKinematic = !isLocal;

        if (auto* cs = m_world.try_get<px::CharacterState>(e))
        {
            desc.cs = *cs;
        }
        else if (auto* rs = m_world.try_get<px::RigidState>(e))
        {
            desc.rs = *rs;
        }

        const px::PhysicsHandle h = m_physics->Spawn(key, desc);
        if (!h.IsValid()) return;

        const px::eBodyKind kind = m_physics->GetKind(key);
        if (kind == px::eBodyKind::NONE) return;
        m_world.emplace_or_replace<NetActorBodyKind>(e, NetActorBodyKind{ kind });

        if (px::IsCharacterBody(kind))
        {
            m_world.emplace_or_replace<CharacterPhysicalBody>(e, CharacterPhysicalBody{ h });

            if (isLocal)
            {
                m_world.emplace_or_replace<PlayerInputState>(e);
                m_world.emplace_or_replace<PlayerTag>(e);
            }
        }
        else
        {
            m_world.emplace_or_replace<RigidPhysicalBody>(e, RigidPhysicalBody{ h });
        }
    }

    void ClientPhysicsSystem::DespawnActor(entt::entity e) const
    {
        if (!m_physics || !m_world.valid(e))
            return;

        const px::ObjectKey key = MakeObjectKey(e);
		m_physics->Despawn(key);

        if (auto* cc = m_world.try_get<CharacterPhysicalBody>(e))
            cc->handle = {};

        if (auto* ra = m_world.try_get<RigidPhysicalBody>(e))
            ra->handle = {};
    }


    // check serverTick freshness for packet reordering/duplication prevention 
    // and inputAck for ack backtracking prevention.
    void ClientPhysicsSystem::CheckAndReconcile()
    {
        auto* queue = m_world.ctx().find<PendingServerStateQueue>();
        if (!queue || queue->states.empty()) return;

        std::optional<ServerState> latestState;

        while (!queue->states.empty())
        {
            auto state = queue->states.front();
            queue->states.pop_front();

            if (state.serverTick <= m_lastReconciledServerTick) continue;
            if (state.inputAck < m_lastReconciledSeq) continue;

            if (!latestState.has_value() || state.serverTick > latestState->serverTick)
                latestState = state;
        }

        if (latestState.has_value())
        {
            Reconcile(latestState.value());
        }
    }

    void ClientPhysicsSystem::PredictCurrentFrame()
    {
        entt::entity player = GetPlayerEntity(m_world);
        if (player == entt::null) return;

        const InputCmd currentInput = m_world.get<PlayerInputState>(player).currentInput;

        const float mag = m_visualPosOffset.Magnitude();
		if (mag > px::EPSILON)
        {
            entt::entity e = GetPlayerEntity(m_world);
            if (e != entt::null && m_world.valid(e))
            {
                const float alpha = std::clamp(m_config.smoothCorrectionAlpha, 0.0f, 1.0f);
                if (alpha > 0.0f)
                {
                    px::Vec3 step = m_visualPosOffset * alpha;

                    constexpr float kMaxSmoothStep = 0.25f;
                    const float stepMag = step.Magnitude();
                    if (stepMag > kMaxSmoothStep && stepMag > 1e-6f)
                        step *= (kMaxSmoothStep / stepMag);

                    m_visualPosOffset -= step;

                    if (m_visualPosOffset.Magnitude() < 1e-4f)
                        m_visualPosOffset = px::Vec3::Zero();
                }
            }
        }

        ApplyInput(currentInput);
        Simulate();
		SavePredictedState(currentInput.seq);
    }

    void ClientPhysicsSystem::SyncTransforms()
    {
        if (!m_physics) return;

        const entt::entity localEntity = GetPlayerEntity(m_world);

        auto view = m_world.view<NetIdentity, NetActorBodyKind>();
        for (auto e : view)
        {
            const px::ObjectKey key = MakeObjectKey(e);
            const auto kind = view.get<NetActorBodyKind>(e).body;

            if (e == localEntity)
            {
                // [로컬 엔티티] PhysX(예측 결과) -> ECS 상태로 동기화
                if (px::IsCharacterBody(kind))
                {
                    auto& state = m_world.get<px::CharacterState>(e);
                    JAMNET_ASSERT(m_physics->GetCharacterState(key, state))

                        // 시각적 보간 오프셋 적용
                	state.pos += m_visualPosOffset;
                }
                else
                {
                    auto& state = m_world.get<px::RigidState>(e);
                    JAMNET_ASSERT(m_physics->GetRigidState(key, state))
                }
            }
            else
            {
                // [리모트 엔티티] ECS 상태(서버 리플리케이션/보간 결과) -> PhysX로 동기화
                if (px::IsCharacterBody(kind))
                {
                    if (auto* state = m_world.try_get<px::CharacterState>(e))
                        m_physics->SetCharacterState(key, *state);
                }
                else
                {
                    if (auto* state = m_world.try_get<px::RigidState>(e))
                        m_physics->SetRigidState(key, *state);
                }
            }
        }
    }

    void ClientPhysicsSystem::Reconcile(const ServerState& serverState)
    {
        entt::entity player = GetPlayerEntity(m_world);
        if (player == entt::null || !m_physics) return;

        const px::ObjectKey key = MakeObjectKey(player);

        // [1] Rewind 이전 현재 예측 위치 저장
        px::CharacterState before{};
        const bool hasBefore = m_physics->GetCharacterState(key, before);

        // [2] 서버 상태로 되감기 + 입력 재생
        RewindToServerState(serverState);
        ReplayInputs(serverState.inputAck);

        // [3] physics는 after에 유지, 시각 오프셋만 분리
        px::CharacterState after{};
        if (hasBefore && m_physics->GetCharacterState(key, after))
        {
        	const px::Vec3 delta = after.pos - before.pos;
            const float posError  = delta.Magnitude();
            const float posThresh = std::max(0.05f, m_config.positionErrorThreshold);

			//JAMNET_LOG_DEBUG("Reconcile | pos error = {}, delta({}, {}, {})", posError, delta.x, delta.y, delta.z);

            if (posError > posThresh)
            {
                constexpr float kSnapThreshold = 1.5f;

                if (posError >= kSnapThreshold)
                {
                    // 오차가 너무 크면 즉시 스냅 (after 위치 그대로)
                    m_visualPosOffset = px::Vec3::Zero();
                }
                else
                {
                    // physics는 after(정확한 위치) 유지
                      // visual은 before처럼 보이도록 offset = before - after
                    m_visualPosOffset += (-delta);

                    // 누적 보정량 클램프
                    constexpr float kMaxAccumulatedCorrection = 2.0f;
                    const float dist = m_visualPosOffset.Magnitude();
                    if (dist > kMaxAccumulatedCorrection && dist > 1e-6f)
                        m_visualPosOffset *= (kMaxAccumulatedCorrection / dist);
                }
            }
            else
            {
                // 오차 없음: 남은 보정도 초기화
                m_visualPosOffset = px::Vec3::Zero();
            }
        }

        PrunePredictedHistory(serverState.inputAck);
        m_lastReconciledSeq = serverState.inputAck;
        m_lastReconciledServerTick = serverState.serverTick;

        if (m_world.ctx().contains<ClientInputSystem>())
            m_world.ctx().get<ClientInputSystem>().OnServerAck(serverState.inputAck);
    }

    void ClientPhysicsSystem::RewindToServerState(const ServerState& serverState)
    {
        if (!m_physics)
            return;

        const entt::entity player = GetPlayerEntity(m_world);
        if (player == entt::null) return;

        const px::ObjectKey key = MakeObjectKey(player);

        m_physics->SetCharacterState(key, serverState.state);
    }

    void ClientPhysicsSystem::ReplayInputs(uint32 fromSeq)
    {
        entt::entity player = GetPlayerEntity(m_world);
        if (player == entt::null) return;

        auto& inputState = m_world.get<PlayerInputState>(player);
        uint32 lastSeq = fromSeq;

        for (const auto& cmd : inputState.unackedInputs)
        {
            if (cmd.seq <= lastSeq || cmd.seq >= inputState.currentInput.seq) continue;
            lastSeq = cmd.seq;

            ApplyInput(cmd);
            Simulate();
        }
    }


    void ClientPhysicsSystem::ApplyInput(const InputCmd& cmd)
    {
        entt::entity player = GetPlayerEntity(m_world);
        if (player == entt::null || !m_world.valid(player) || !m_physics)
            return;

        const px::ObjectKey key = MakeObjectKey(player);

        m_physics->ApplyCharacterInput(key, cmd.input);
    }


    void ClientPhysicsSystem::Simulate()
    {
        if (!m_physics) return;

        auto& shard = SHARD_LOCAL_CHECKED();
        auto* sched = shard.scheduler;

        const bool inFiber = sched && (sched->Current() != 0);

        const uint64 awaitKey = inFiber ? ++m_awaitSeq : 0;

        // BeginStep이 true를 반환하면 PhysX Task가 Shard에 제출되었으므로 파이버를 Suspend 합니다.
        if (m_physics->BeginStep(SIMULATION_TICK_SEC, awaitKey) && inFiber)
            sched->Suspend(awaitKey, NOW_NS() + 2_s);

        // 파이버가 Resume 된 후 (또는 동기 실행 시) 결과를 가져옵니다.
        m_physics->EndStep();
    }


    void ClientPhysicsSystem::SavePredictedState(uint32 inputSeq)
    {
        if (!m_physics) return;

        entt::entity player = GetPlayerEntity(m_world);
        if (player == entt::null) return;

        const px::ObjectKey key = MakeObjectKey(player);

        auto& cs = m_world.get<px::CharacterState>(player);
        JAMNET_ASSERT(m_physics->GetCharacterState(key, cs))

        PredictedState st{};
        st.inputSeq = inputSeq;
        st.state    = cs;

        //JAMNET_LOG_DEBUG("Save PredictedState | pos({}, {}, {})", cs.pos.x, cs.pos.y, cs.pos.z);

        m_predictedHistory.push_back(st);

        while (m_predictedHistory.size() > kMaxPredictedHistroySize)
            m_predictedHistory.pop_front();
    }

    optional<PredictedState> ClientPhysicsSystem::GetPredictedState(uint32 inputSeq) const
    {
        for (const auto& state : m_predictedHistory)
        {
            if (state.inputSeq == inputSeq)
                return state;
        }
        return std::nullopt;
    }

    void ClientPhysicsSystem::PrunePredictedHistory(uint32 upToSeq)
    {
        while (!m_predictedHistory.empty() && m_predictedHistory.front().inputSeq <= upToSeq)
        {
            m_predictedHistory.pop_front();
        }
    }

    px::CharacterState* ClientPhysicsSystem::GetLocalCharacterState() const
    {
        entt::entity local = GetPlayerEntity(m_world);
        if (local == entt::null)
            return nullptr;
        return m_world.try_get<px::CharacterState>(local);
    }

    float ClientPhysicsSystem::CalculateRotationError(const px::Quat& a, const px::Quat& b) const
    {
        const float dot = std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
        return 2.0f * std::acos(std::min<float>(dot, 1.0f));
    }
}
