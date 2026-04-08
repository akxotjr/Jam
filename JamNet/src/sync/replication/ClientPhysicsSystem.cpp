#include "pch.h"
#include "jamnet/sync/replication/ClientPhysicsSystem.h"
#include "jamnet/sync/replication/NetActorComponents.h"
#include "jamnet/sync/replication/NetWorldContext.h"
#include "jamnet/sync/replication/CorrectionReplayRunner.h"
#include "jamnet/sync/networld/ClientNetWorld.h"

#include <jampx/PhysicsTypes.h>
#include <jampx/IPhysicsFacade.h>


namespace jam::net
{

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

        auto& shard = SHARD_LOCAL_CHECKED();
        auto* sched = shard.scheduler;

        if (!sched) // if no scheduler then sync-path
        {
            runOneTick();
            return;
        }

        if (m_tickFiberRunning) // prevent dup-execution if prev fiber is still waiting/running
            return;

        m_tickFiberRunning = true;

        sched->SpawnFiber(
            [this, runOneTick]()
            {
                try
                {
                    runOneTick();
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
        const px::ObjectId oid   = MakeObjectId(local);
        const auto& currentInput = m_world.ctx().get<InputHistoryBuffer>().current;

        m_physics->ApplyCharacterInput(oid, currentInput.input);
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

        const uint32 currentTick = m_world.ctx().get<TickCounter>().tick;
        const uint32 inputAck    = signal.inputAck;

        const ReplayContext rc{ .tick = currentTick, .local = local, .inputAck = inputAck };

        Rewind(rc);
        Replay(rc);

        m_replayRunner->Commit(m_world, rc);

        m_world.ctx().get<InputHistoryBuffer>().PruneAck(inputAck);
        m_world.ctx().get<PredictedHistoryBuffer>().PruneAck(inputAck);
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

        const uint32 currentSeq = inputHistory.current.seq;
        if (currentSeq > ctx.tick)
            return;

        uint32 replayCount = 0;

        inputHistory.ForEachReplayRange(ctx.inputAck, currentSeq,
            [this, local, oid, &replayCount, &replayBuf](const InputCmd& cmd)
            {
                if (replayCount >= m_config.maxReplayInputs)
                    return;

                ReplayContext step{ .tick = cmd.seq, .local = local, .inputAck = 0 };
                m_replayRunner->Run(m_world, step);

                ApplyInput(cmd);
                Resimulate();

                px::CharacterState cs{};
                m_physics->PullCorrectionState(oid, cs);
                replayBuf.Push(cmd.seq, cs);

                ++replayCount;
            });
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
