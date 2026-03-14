#include "pch.h"
#include "jamnet/sync/replication/ClientPhysicsSystem.h"
#include "jamnet/sync/replication/NetActorComponents.h"
#include "jamnet/sync/replication/ClientInputSystem.h"
#include "jamnet/sync/replication/ClientReplicationSystem.h"
#include "jamnet/sync/networld/ClientNetWorld.h"

#include <jampx/PhysicsTypes.h>
#include <jampx/IPhysicsFacade.h>

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

        const px::ObjectId id = MakeObjectId(e);

        px::SpawnDesc desc{};
        desc.prefab      = pk->key;
		desc.spawnSrc    = px::eSpawnSource::Network;
		desc.team = 0;	// TODO: team/part/role 정보가 필요한 경우 NetActorComponents에 추가하여 설정
		desc.part = 0;
		desc.role = 0;

        if (auto* cs = m_world.try_get<px::CharacterState>(e))
        {
            desc.pose       = px::Transform{ .p = cs->pos, .q = px::Quat::Identity() };
            desc.overrides  = px::CharacterSpawnOverrides{
                .mask   = px::SpawnOverrideMask::VIEW_YAW | px::SpawnOverrideMask::VIEW_PITCH,
                .yaw    = cs->facingYaw,
                .pitch  = cs->facingPitch
			};
        }
        else if (auto* rs = m_world.try_get<px::RigidState>(e))
        {
			desc.pose = px::Transform{ .p = rs->pose.p, .q = rs->pose.q };
            desc.overrides = px::RigidSpawnOverrides{
                .mask               = px::SpawnOverrideMask::LINEAR_VEL | px::SpawnOverrideMask::ANGULAR_VEL | px::SpawnOverrideMask::LINEAR_DAMP | px::SpawnOverrideMask::ANGULAR_DAMP,
                .linearVelocity     = rs->linVel,
                .angularVelocity    = rs->angVel,
                .linearDamping      = 0.0f,	// 서버에서 리플리케이션 시점의 감쇠값을 알 수 없으므로 일단 0으로 스폰, 이후 서버 상태로 보정
                .angularDamping     = 0.0f,
			};
        }

        const px::PhysicsHandle h = m_physics->Spawn(id, desc);
        if (!h.IsValid()) return;

		const px::eBodyType bodyType = desc.IsCharacter() ? px::eBodyType::Character : px::eBodyType::Rigid;
        m_world.emplace_or_replace<NetActorBodyType>(e, NetActorBodyType{ bodyType });

        if (bodyType == px::eBodyType::Character)
        {
            m_world.emplace_or_replace<CharacterPhysicalBody>(e, CharacterPhysicalBody{ h });

            if (isLocal)
            {
                m_world.emplace_or_replace<LocalInputState>(e);
                m_world.emplace_or_replace<LocalCharacterTag>(e);
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

        const px::ObjectId id = MakeObjectId(e);
		m_physics->Despawn(id);

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
        entt::entity player = GetLocalEntity(m_world);
        if (player == entt::null) return;

        const InputCmd currentInput = m_world.get<LocalInputState>(player).currentInput;

        const float mag = m_visualPosOffset.Magnitude();
		if (mag > px::EPSILON)
        {
            entt::entity e = GetLocalEntity(m_world);
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

        const entt::entity local = GetLocalEntity(m_world);

        auto view = m_world.view<NetIdentity, NetActorBodyType>();
        for (auto e : view)
        {
            const px::ObjectId id = MakeObjectId(e);
            const auto bodyType = view.get<NetActorBodyType>(e).body;

            if (e == local)
            {
                // [로컬 엔티티] PhysX(예측 결과) -> ECS 상태로 동기화
                auto& state = m_world.get<px::CharacterState>(e);
                JAM_ASSERT(m_physics->GetCharacterState(id, state))

                    // 시각적 보간 오프셋 적용
                state.pos += m_visualPosOffset;
            }
            else
            {
                // [리모트 엔티티] ECS 상태(서버 리플리케이션/보간 결과) -> PhysX로 동기화
                if (bodyType == px::eBodyType::Character)
                {
                    if (auto* state = m_world.try_get<px::CharacterState>(e))
                        m_physics->SetCharacterState(id, *state);
                }
                else
                {
                    if (auto* state = m_world.try_get<px::RigidState>(e))
                        m_physics->SetRigidState(id, *state);
                }
            }
        }
    }

    void ClientPhysicsSystem::Reconcile(const ServerState& serverState)
    {
        entt::entity player = GetLocalEntity(m_world);
        if (player == entt::null || !m_physics) return;

        const px::ObjectId id = MakeObjectId(player);

        // [1] Rewind 이전 현재 예측 위치 저장
        px::CharacterState before{};
        const bool hasBefore = m_physics->GetCharacterState(id, before);

        // [2] 서버 상태로 되감기 + 입력 재생
        RewindToServerState(serverState);
        ReplayInputs(serverState.inputAck);

        // [3] physics는 after에 유지, 시각 오프셋만 분리
        px::CharacterState after{};
        if (hasBefore && m_physics->GetCharacterState(id, after))
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

        const entt::entity player = GetLocalEntity(m_world);
        if (player == entt::null) return;

        const px::ObjectId id = MakeObjectId(player);

        m_physics->SetCharacterState(id, serverState.state);
    }

    void ClientPhysicsSystem::ReplayInputs(uint32 fromSeq)
    {
        entt::entity player = GetLocalEntity(m_world);
        if (player == entt::null) return;

        auto& inputState = m_world.get<LocalInputState>(player);
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
        entt::entity player = GetLocalEntity(m_world);
        if (player == entt::null || !m_world.valid(player) || !m_physics)
            return;

        const px::ObjectId id = MakeObjectId(player);

        m_physics->ApplyCharacterInput(id, cmd.input);
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

        entt::entity player = GetLocalEntity(m_world);
        if (player == entt::null) return;

        const px::ObjectId id = MakeObjectId(player);

        auto& cs = m_world.get<px::CharacterState>(player);
        JAM_ASSERT(m_physics->GetCharacterState(id, cs))

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
        entt::entity player = GetLocalEntity(m_world);
        if (player == entt::null)
            return nullptr;
        return m_world.try_get<px::CharacterState>(player);
    }

    float ClientPhysicsSystem::CalculateRotationError(const px::Quat& a, const px::Quat& b) const
    {
        const float dot = std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
        return 2.0f * std::acos(std::min<float>(dot, 1.0f));
    }
}
