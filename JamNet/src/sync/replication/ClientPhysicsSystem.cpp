#include "pch.h"
#include "jamnet/sync/replication/ClientPhysicsSystem.h"
#include "jamnet/sync/replication/NetActorComponents.h"
#include "jamnet/sync/replication/NetWorldContext.h"
#include "jamnet/sync/replication/CorrectionReplayRunner.h"
#include "jamnet/sync/networld/ClientNetWorld.h"

#include <jampx/PhysicsTypes.h>
#include <jampx/IPhysicsFacade.h>
#include <cmath>


namespace jam::net
{
	namespace
	{
		constexpr uint32 kDirectionalMask =
			px::INPUT_FORWARD | px::INPUT_BACKWARD | px::INPUT_LEFT | px::INPUT_RIGHT | px::INPUT_RUN;

		const px::CharacterState* ResolveCharacterState(const entt::registry& world, entt::entity e)
		{
			if (const auto* proxy = world.try_get<CharProxyState>(e))
				return &proxy->state;
			if (const auto* auth = world.try_get<CharAuthorityState>(e))
				return &auth->state;
			return nullptr;
		}

		bool ResolveActorTargetPos(const entt::registry& world, entt::entity e, OUT px::Vec3& outPos)
		{
			if (const auto* proxy = world.try_get<CharProxyState>(e))
			{
				outPos = proxy->state.pos;
				return true;
			}
			if (const auto* auth = world.try_get<CharAuthorityState>(e))
			{
				outPos = auth->state.pos;
				return true;
			}
			if (const auto* proxy = world.try_get<RigidProxyState>(e))
			{
				outPos = proxy->state.pose.p;
				return true;
			}
			if (const auto* auth = world.try_get<RigidAuthorityState>(e))
			{
				outPos = auth->state.pose.p;
				return true;
			}
			return false;
		}

		bool HasMeaningfulReplayInput(const px::CharacterInput& input)
		{
			if (input.inputFlags != px::INPUT_NONE)
				return true;

			if (input.moveMode == px::eMoveInputMode::Mouse)
			{
				if (input.mouseMoveKind == px::eMouseMoveKind::FollowTarget)
					return input.targetNetId != 0;

				return input.targetPos.MagnitudeSquared() > 0.0f;
			}

			return false;
		}
	}

	ClientPhysicsSystem::ClientPhysicsSystem(entt::registry& world, px::IPhysicsFacade* physics)
		: m_world(world), m_physics(physics)
	{
	}

    void ClientPhysicsSystem::Init()
	{
        m_replayRunner = std::make_unique<CorrectionReplayRunner>(m_physics);

        if (auto* nw = m_world.ctx().find<ClientNetWorld*>())
            m_userId = (*nw) ? (*nw)->GetUserId() : 0;
    }

    void ClientPhysicsSystem::Tick()
    {
        if (!m_physics) return;

        auto runOneTick = [this]()
            {
                Reconcile();
                PushAuthorityStates();
                LivePredict();
                PullProxyStates();
                HandleProjectileLifecycleEvents();
            };

        if (m_tickDebt < m_tickDebtCap)
            ++m_tickDebt;

        auto& shard = SHARD_LOCAL_CHECKED();
        auto* sched = shard.scheduler;

        if (!sched) // if no scheduler then sync-path
        {
            while (m_tickDebt > 0)
            {
                --m_tickDebt;
                runOneTick();
            }
            return;
        }

        if (m_tickFiberRunning)
            return;

        m_tickFiberRunning = true;

        sched->SpawnFiber(
            [this, runOneTick]()
            {
                try
                {
                    uint32 burst = 0;
                    while (m_tickDebt > 0 && burst < m_tickBurstBudget)
                    {
                        --m_tickDebt;
                        runOneTick();
                        ++burst;
                    }
                }
                catch (...)
                {
                    m_tickFiberRunning = false;
                    throw;
                }

                m_tickFiberRunning = false;
            },
            FiberDesc{ .name = "ClientPhysicsSystem.TickFiber" }
        );
    }

    void ClientPhysicsSystem::SpawnActor(entt::entity e, bool isLocal) const
    {
        if (!m_physics || !m_world.valid(e)) return;

        const auto& pk = m_world.get<NetPrefabKey>(e);
        if (!pk.key.IsValid()) return;

        const px::ObjectId id = MakeObjectId(e);

        px::SpawnDesc desc{};
        desc.prefab     = pk.key;
        desc.spawnSrc   = isLocal ? px::eSpawnSource::Runtime : px::eSpawnSource::Network;

        const auto& tpr = m_world.get<NetTeamPartRole>(e);
        desc.team = tpr.team;
        desc.part = tpr.part;
        desc.role = tpr.role;

        const px::eBodyType bodyType = m_world.get<NetActorBodyType>(e).body;
        const bool isRigid = (bodyType == px::eBodyType::Rigid);

        if (isRigid)
        {
            const auto& [rs] = m_world.get<RigidAuthorityState>(e);
            desc.pose      = rs.pose;
            desc.overrides = px::RigidSpawnOverrides{};
        }
        else
        {
            const auto& [cs] = m_world.get<CharAuthorityState>(e);
            desc.pose = { .p = cs.pos };
            desc.overrides = px::CharacterSpawnOverrides{
                .mask  = px::SpawnOverrideMask::VIEW_YAW | px::SpawnOverrideMask::VIEW_PITCH,
                .yaw   = cs.facingYaw,
                .pitch = cs.facingPitch
            };
        }

        if (auto* target = m_world.try_get<TargetInfo>(e))
        {
            desc.targetId = target->targetObjId;
        }

        if (!m_physics->Spawn(id, desc)) return;

        if (m_physics->IsStepPending())
        {
            m_pendingActorOps.push_back(PendingActorOp{
                .type    = PendingActorOp::eType::Spawn,
                .e       = e,
                .isLocal = isLocal,
                .isRigid = isRigid
            });
            return;
        }

        m_world.emplace_or_replace<PhysicsSpawnedTag>(e);

        if (isLocal)
        {
            if (!isRigid) 
                m_world.emplace_or_replace<LocalActorTag>(e);
        }
        else
        {
            m_world.emplace_or_replace<RemoteActorTag>(e);
        }
    }

    void ClientPhysicsSystem::DespawnActor(entt::entity e) const
    {
        if (!m_physics || !m_world.valid(e))
            return;

        const px::ObjectId id = MakeObjectId(e);
        if (!m_physics->Despawn(id))
            return;

        if (m_physics->IsStepPending())
        {
            m_pendingActorOps.push_back(PendingActorOp{
                .type = PendingActorOp::eType::Despawn,
                .e    = e
            });
            return;
        }

        m_world.erase<PhysicsSpawnedTag>(e);
    }


    void ClientPhysicsSystem::LivePredict()
    {
        if (!m_physics) return;

        const entt::entity local = GetLocalEntity(m_world);
        if (local == entt::null || !m_world.valid(local))
            return;

        const px::ObjectId oid   = MakeObjectId(local);
        const auto& currentInput = m_world.ctx().get<InputHistoryBuffer>().current;

        const auto* selfState = ResolveCharacterState(m_world, local);
        const px::CharacterInput resolvedInput = ResolveInputForSimulation(currentInput.input, selfState);

        m_physics->ApplyCharacterInput(oid, resolvedInput);
        Simulate();

        auto& live = m_world.ctx().get<LivePredictedState>();
        m_physics->PullPredictedState(oid, live);

        m_world.ctx().get<PredictedHistoryBuffer>().Push(currentInput.seq, live);
    }

    void ClientPhysicsSystem::Reconcile()
    {
        const entt::entity local = GetLocalEntity(m_world);
        if (local == entt::null || !m_physics || !m_replayRunner) return;

        auto& signal = m_world.ctx().get<ReconcileSignal>();
        if (!signal.dirty) return;
        signal.dirty = false;

        //const uint32 currentTick = m_world.ctx().get<TickCounter>().tick;
        const uint32 inputAck    = signal.inputAck;
        auto& inputHistory       = m_world.ctx().get<InputHistoryBuffer>();
        auto& predictedHistory   = m_world.ctx().get<PredictedHistoryBuffer>();

        if (!m_world.all_of<CharAuthorityState>(local))
        {
            inputHistory.PruneAck(inputAck);
            predictedHistory.PruneAck(inputAck);
            return;
        }

        const auto& auth = m_world.get<CharAuthorityState>(local).state;
        const auto& live = m_world.ctx().get<LivePredictedState>();

        auto absAngleDelta = [](float a, float b)
            {
                float d = std::fmod(a - b, px::TWO_PI);
                if (d > px::PI)  d -= px::TWO_PI;
                if (d < -px::PI) d += px::TWO_PI;
                return std::abs(d);
            };

        const float posErr   = (auth.pos - live.pos).Magnitude();
        const float yawErr   = absAngleDelta(auth.facingYaw, live.facingYaw);
        const float pitchErr = absAngleDelta(auth.facingPitch, live.facingPitch);

        const bool withinThreshold =
        	(posErr <= m_config.positionErrorThreshold)
            &&  (yawErr <= m_config.rotationErrorThreshold)
            &&  (pitchErr <= m_config.rotationErrorThreshold);

        if (withinThreshold)
        {
            auto& correction = m_world.ctx().get<CorrectionState>();
            auto& delta      = m_world.ctx().get<RenderCorrectionDelta>();

            correction = live;
            delta = {};

            inputHistory.PruneAck(inputAck);
            predictedHistory.PruneAck(inputAck);
            return;
        }

        const ReplayContext rc{ 
        	.tick     = static_cast<uint32>(signal.serverTick), 
        	.local    = local, 
        	.inputAck = inputAck };

        Rewind(rc);
        Replay(rc);

        m_replayRunner->Commit(m_world, rc);

        inputHistory.PruneAck(inputAck);
        predictedHistory.PruneAck(inputAck);
    }



    void ClientPhysicsSystem::PushAuthorityStates()
    {
        if (!m_physics) return;

        const entt::entity local = GetLocalEntity(m_world);
        std::vector<px::ActorContext> contexts;
        contexts.reserve(64);

        auto view = m_world.view<NetId, NetActorBodyType>();
        for (auto e : view)
        {
            if (e == local) continue;

            px::ActorContext ac{};
            ac.oid = MakeObjectId(e);

            const auto bodyType = view.get<NetActorBodyType>(e).body;
            if (bodyType == px::eBodyType::Character)
            {
                ac.state = m_world.get<CharAuthorityState>(e).state;
                contexts.push_back(ac);
            }
            else
            {
                const auto& auth = m_world.get<RigidAuthorityState>(e);
                if (!px::IsLocalDrivenKine(auth.state.kineType))
                {
                    ac.state = auth.state;
                    contexts.push_back(ac);
                }
            }
        }

        if (!contexts.empty())
            m_physics->PushAuthorityStates(contexts);
    }


    void ClientPhysicsSystem::PullProxyStates()
    {
        if (!m_physics) return;

        std::vector<px::ActorContext> contexts;
        auto view = m_world.view<NetId>();

        for (auto e : view)
        {
            px::ActorContext ac{};
            ac.oid = MakeObjectId(e);
            contexts.push_back(ac);
        }

        m_physics->PullProxyStates(contexts);

        for (auto& ac : contexts)
        {
            const entt::entity e = static_cast<entt::entity>(ac.oid);
            if (!m_world.valid(e) || !m_world.all_of<NetActorBodyType>(e))
                continue;

            const auto bodyType = m_world.get<NetActorBodyType>(e).body;

            if (bodyType == px::eBodyType::Character)
            {
                auto* cs = std::get_if<px::CharacterState>(&ac.state);
                if (!cs) continue;

                auto& [proxy] = m_world.get<CharProxyState>(e);
                proxy = *cs;
            }
            else
            {
                auto* rs = std::get_if<px::RigidState>(&ac.state);
                if (!rs) continue;

                auto& [proxy] = m_world.get<RigidProxyState>(e);
                proxy = *rs;

                // local driven kine 인 경우 일단은 proxy 로 덮어쓰는데 어떻게 해야되지?
                if (px::IsLocalDrivenKine(proxy.kineType))
                {
                    auto& [auth] = m_world.get<RigidAuthorityState>(e);
                    auth = proxy;
                }
            }
        }
    }


    void ClientPhysicsSystem::Rewind(const ReplayContext& ctx)
    {
        m_replayRunner->Prepare(m_world, ctx);
    }

    void ClientPhysicsSystem::Replay(const ReplayContext& ctx)
    {
        const entt::entity local = GetLocalEntity(m_world);
        if (local == entt::null || !m_world.valid(local)) return;

        const px::ObjectId oid = MakeObjectId(local);
        auto& inputHistory = m_world.ctx().get<InputHistoryBuffer>();
        auto& replayBuf    = m_world.ctx().get<ReplayPredictedBuffer>();
        auto& replayStats  = m_world.ctx().get<ReplayStats>();

        const uint32 currentSeq = inputHistory.current.seq;

        replayStats = {};

        uint32 replayCandidateCount = 0;
        uint32 replayStepCount = 0;
        uint32 replayMeaningfulInputCount = 0;

        inputHistory.ForEachReplayRange(ctx.inputAck, currentSeq,
            [this, local, oid, &replayCandidateCount, &replayStepCount, &replayMeaningfulInputCount, &replayBuf, &ctx](const InputCmd& cmd)
            {
                ++replayCandidateCount;

                if (replayStepCount >= m_config.maxReplayInputs)
                    return;

                if (HasMeaningfulReplayInput(cmd.input))
                    ++replayMeaningfulInputCount;

                ApplyInput(cmd);

                ReplayContext step{ .tick = cmd.seq, .local = local, .inputAck = ctx.inputAck };
                m_replayRunner->Run(m_world, step);
                Resimulate();

                px::CharacterState cs{};
                m_physics->PullCorrectionState(oid, cs);
                replayBuf.Push(cmd.seq, cs);

                ++replayStepCount;
            });

        replayStats.stepCount            = replayStepCount;
        replayStats.meaningfulInputCount = replayMeaningfulInputCount;
        replayStats.truncated            = replayCandidateCount > replayStepCount;

        JAMNET_LOG_DEBUG(
            "[ClientPhysicsSystem] replay steps= {}, meaningfulInputs= {}, truncated= {}, inputAck= {}, currentSeq= {}",
            replayStepCount,
            replayMeaningfulInputCount,
            replayStats.truncated,
            ctx.inputAck,
            currentSeq);
    }


    void ClientPhysicsSystem::ApplyInput(const InputCmd& cmd)
    {
        entt::entity player = GetLocalEntity(m_world);
        if (player == entt::null || !m_world.valid(player) || !m_physics)
            return;

        const px::ObjectId id = MakeObjectId(player);
        const auto* selfState = ResolveCharacterState(m_world, player);
        const px::CharacterInput resolvedInput = ResolveInputForSimulation(cmd.input, selfState);
        m_physics->ApplyCharacterInput(id, resolvedInput);
    }

    px::CharacterInput ClientPhysicsSystem::ResolveInputForSimulation(const px::CharacterInput& input, const px::CharacterState* selfState) const
    {
        if (input.moveMode != px::eMoveInputMode::Mouse)
            return input;

        px::CharacterInput resolved = input;
        const uint32 nonDirectional = input.inputFlags & ~kDirectionalMask;
        resolved.inputFlags = nonDirectional;

        if (!selfState)
            return resolved;

        px::Vec3 targetPos = input.targetPos;
        if (input.mouseMoveKind == px::eMouseMoveKind::FollowTarget)
        {
            if (!TryResolveTargetPos(input.targetNetId, targetPos))
            {
                // stop policy: follow target disappeared/unresolvable
                return resolved;
            }
        }

        px::Vec3 toTarget = targetPos - selfState->pos;
        toTarget.y = 0.0f;

        const float distSq = toTarget.MagnitudeSquared();
        constexpr float kStopRadius = 1.1f;
        if (distSq <= (kStopRadius * kStopRadius))
            return resolved;

        resolved.facingYaw   = std::atan2(toTarget.x, toTarget.z);
        resolved.facingPitch = 0.0f;
        resolved.inputFlags  |= px::INPUT_FORWARD;
        if (distSq > 100.0f)
            resolved.inputFlags |= px::INPUT_RUN;
        return resolved;
    }

    bool ClientPhysicsSystem::TryResolveTargetPos(uint32 targetNetIdRaw, OUT px::Vec3& outPos) const
    {
        if (targetNetIdRaw == 0)
            return false;

        const NetId targetNetId = NetId::MakeRaw(targetNetIdRaw);
        auto view = m_world.view<NetId>();
        for (const entt::entity e : view)
        {
            if (view.get<NetId>(e) != targetNetId)
                continue;

            return ResolveActorTargetPos(m_world, e, outPos);
        }
        return false;
    }


    void ClientPhysicsSystem::Simulate()
    {
        if (!m_physics) return;

        auto& shard = SHARD_LOCAL_CHECKED();
        auto* sched = shard.scheduler;

        const bool   inFiber  = sched && (sched->Current() != 0);
        const uint64 awaitKey = inFiber ? ++m_awaitSeq : 0;

        if (m_physics->BeginSimulate(SIMULATION_TICK_SEC, awaitKey) && inFiber)
            sched->Suspend(awaitKey, NOW_NS() + 1_s);

        m_physics->EndSimulate();
        CommitPendingActorOps();
    }

    void ClientPhysicsSystem::Resimulate()
    {
        if (!m_physics) return;

        auto& shard = SHARD_LOCAL_CHECKED();
        auto* sched = shard.scheduler;

        const bool   inFiber  = sched && (sched->Current() != 0);
        const uint64 awaitKey = inFiber ? ++m_awaitSeq : 0;

        if (m_physics->BeginResimulate(SIMULATION_TICK_SEC, awaitKey) && inFiber)
            sched->Suspend(awaitKey, NOW_NS() + 1_s);
        
        m_physics->EndResimulate();
        CommitPendingActorOps();
    }

    void ClientPhysicsSystem::CommitPendingActorOps() const
    {
        if (m_pendingActorOps.empty())
            return;

        auto ops = std::move(m_pendingActorOps);
        m_pendingActorOps.clear();

        for (const auto& op : ops)
        {
            if (!m_world.valid(op.e)) continue;

            if (op.type == PendingActorOp::eType::Spawn)
            {
                m_world.emplace_or_replace<PhysicsSpawnedTag>(op.e);

                if (op.isLocal)
                {
                    if (!op.isRigid)
                        m_world.emplace_or_replace<LocalActorTag>(op.e);
                }
                else
                {
                    m_world.emplace_or_replace<RemoteActorTag>(op.e);
                }
            }
            else
            {
                m_world.erase<PhysicsSpawnedTag>(op.e);
            }
        }
    }

    void ClientPhysicsSystem::HandleProjectileLifecycleEvents()
    {
        if (!m_physics)
            return;

        auto* nwPtr = m_world.ctx().find<ClientNetWorld*>();
        if (!nwPtr || !*nwPtr)
            return;

        ClientNetWorld* netWorld = *nwPtr;

        for (const px::PhysicsEvent& evt : m_physics->ConsumePhysicsEvents())
        {
            if (evt.type != px::ePhysicsEventType::ProjectileLifetimeExpired)
                continue;

            const entt::entity e = static_cast<entt::entity>(evt.sourceId);
            if (e == entt::null || !m_world.valid(e))
                continue;

            const auto* owner = m_world.try_get<OwnershipTag>(e);
            const auto* netId = m_world.try_get<NetId>(e);
            if (!owner || !netId || !netId->IsValid())
                continue;

            if (owner->userId == 0 || owner->userId != m_userId)
                continue;

            netWorld->PredictReplicatedActorDespawn(*netId);
        }
    }
}
