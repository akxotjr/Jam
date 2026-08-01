#include "pch.h"
#include "jamnet/runtime/world/simulation/client/ClientPhysicsSystem.h"
#include "jamnet/runtime/world/simulation/common/ActorComponents.h"
#include "jamnet/runtime/world/simulation/common/WorldContext.h"
#include "jamnet/runtime/world/simulation/common/CorrectionReplayRunner.h"
#include "jamnet/runtime/world/simulation/client/ClientWorld.h"
#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/core/executor/ShardExecutor.h"

#include <jampx/PhysicsTypes.h>
#include <jampx/PhysicsFacade.h>

#include <limits>


namespace jam::net
{
	namespace
	{
		const px::CharacterState* ResolveCharacterState(const entt::registry& world, entt::entity e)
		{
			if (const auto* proxy = world.try_get<CharProxyState>(e))
				return &proxy->state;
			if (const auto* auth = world.try_get<CharAuthorityState>(e))
				return &auth->state;
			return nullptr;
		}

		const px::CharacterState* ResolveReplayCharacterState(const entt::registry& world, entt::entity e)
		{
			if (const auto* auth = world.try_get<CharAuthorityState>(e))
				return &auth->state;
			if (const auto* proxy = world.try_get<CharProxyState>(e))
				return &proxy->state;
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

		bool ResolveReplayActorTargetPos(const entt::registry& world, entt::entity e, OUT px::Vec3& outPos)
		{
			if (const auto* auth = world.try_get<CharAuthorityState>(e))
			{
				outPos = auth->state.pos;
				return true;
			}
			if (const auto* proxy = world.try_get<CharProxyState>(e))
			{
				outPos = proxy->state.pos;
				return true;
			}
			if (const auto* auth = world.try_get<RigidAuthorityState>(e))
			{
				outPos = auth->state.pose.p;
				return true;
			}
			if (const auto* proxy = world.try_get<RigidProxyState>(e))
			{
				outPos = proxy->state.pose.p;
				return true;
			}
			return false;
		}

		bool HasMeaningfulReplayInput(const CharacterControlIntent& intent)
		{
			return intent.ActionsForTick() != 0 || !std::holds_alternative<StopMovementIntent>(intent.locomotion);
		}
	}

	ClientPhysicsSystem::ClientPhysicsSystem(entt::registry& world, px::PhysicsFacade* physics)
		: m_world(world), m_physics(physics)
	{
	}

    void ClientPhysicsSystem::Init()
    {
        m_completedTickCount = 0;
        m_replayRunner = std::make_unique<CorrectionReplayRunner>(m_physics);

        if (auto* nw = m_world.ctx().find<ClientWorld*>())
            m_userId = (*nw) ? (*nw)->GetUserId() : 0;
    }

    void ClientPhysicsSystem::Tick()
    {
        if (!m_physics) return;

        m_tickFiberRunning = true;
        try
        {
            Reconcile();
            PushAuthorityStates();
            LivePredict();
            PullProxyStates();
            HandleProjectileLifecycleEvents();
            ++m_completedTickCount;
        }
        catch (...)
        {
            m_tickFiberRunning = false;
            throw;
        }
        m_tickFiberRunning = false;
    }

    void ClientPhysicsSystem::SpawnActor(entt::entity e, bool isLocal) const
    {
        if (!m_physics || !m_world.valid(e)) return;

        const auto& pk = m_world.get<PhysicsArchetypeRef>(e);
        if (!IsValidAssetKey(pk.key)) return;

        const px::ActorId id = GetPhysicsActorId(m_world, e);

        px::SpawnDesc desc{};
        desc.archetype  = pk.key;
        desc.spawnSrc   = isLocal ? px::eSpawnSource::Runtime : px::eSpawnSource::Network;

        const auto& tpr = m_world.get<ActorTeamPartRole>(e);
        desc.team = tpr.team;
        desc.part = tpr.part;
        desc.role = tpr.role;

        const px::eBodyType bodyType = m_world.get<ActorBodyType>(e).body;
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
                .mask  = px::SpawnOverrideMask::BODY_YAW | px::SpawnOverrideMask::VIEW_YAW | px::SpawnOverrideMask::VIEW_PITCH,
				.bodyYaw = cs.bodyYaw,
				.yaw   = cs.viewYaw,
                .pitch = cs.viewPitch
            };
        }

        if (auto* target = m_world.try_get<TargetInfo>(e))
        {
			desc.targetActorId = target->resolvedActorId;
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

        const px::ActorId id = GetPhysicsActorId(m_world, e);
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

        const entt::entity local = GetCachedLocalEntity(m_world);
        if (local == entt::null || !m_world.valid(local))
            return;

        const px::ActorId physicsActorId   = GetPhysicsActorId(m_world, local);
        const auto& currentInput = m_world.ctx().get<InputHistoryBuffer>().current;

        const auto* selfState = ResolveCharacterState(m_world, local);
        const px::CharacterMotorInput resolvedInput = ResolveInputForSimulation(currentInput.intent, selfState, false);

        m_physics->ApplyCharacterMotorInput(physicsActorId, resolvedInput);
        Simulate();

        auto& live = m_world.ctx().get<LivePredictedState>();
        m_physics->PullPredictedState(physicsActorId, live);

        m_world.ctx().get<PredictedHistoryBuffer>().Push(currentInput.sequence, live);
    }

    void ClientPhysicsSystem::Reconcile()
    {
        const entt::entity local = GetCachedLocalEntity(m_world);
        if (local == entt::null || !m_physics || !m_replayRunner) return;

        auto& signal = m_world.ctx().get<ReconcileSignal>();
        if (!signal.dirty) return;
        signal.dirty = false;

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
        const uint32 currentSeq = inputHistory.current.sequence;
		const px::CharacterState* predictedAtAck = predictedHistory.Find(inputAck);

		const float posErr = predictedAtAck
			? (auth.pos - predictedAtAck->pos).Magnitude()
			: std::numeric_limits<float>::infinity();

        const bool withinThreshold = posErr <= m_config.positionErrorThreshold;

        if (withinThreshold)
        {
            auto& correction = m_world.ctx().get<CorrectionState>();
            correction = live;

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

        const entt::entity local = GetCachedLocalEntity(m_world);
        m_pushContexts.clear();

        auto view = m_world.view<ActorId, ActorBodyType>();
        m_pushContexts.reserve(view.size_hint());
        for (auto e : view)
        {
            if (e == local) continue;

            px::ActorContext ac{};
            ac.actorId = GetPhysicsActorId(m_world, e);

            const auto bodyType = view.get<ActorBodyType>(e).body;
            if (bodyType == px::eBodyType::Character)
            {
                ac.state = m_world.get<CharAuthorityState>(e).state;
                m_pushContexts.push_back(ac);
            }
            else
            {
                const auto& auth = m_world.get<RigidAuthorityState>(e);
                if (!px::IsLocalDrivenKine(auth.state.kineType))
                {
                    ac.state = auth.state;
                    m_pushContexts.push_back(ac);
                }
            }
        }

        if (!m_pushContexts.empty())
            m_physics->PushAuthorityStates(m_pushContexts);
    }


    void ClientPhysicsSystem::PullProxyStates()
    {
        if (!m_physics) return;

        auto* nwPtr = m_world.ctx().find<ClientWorld*>();
        if (!nwPtr || !*nwPtr)
            return;

        m_pullContexts.clear();
        auto view = m_world.view<ActorId>();
        m_pullContexts.reserve(64);

        for (auto e : view)
        {
            px::ActorContext ac{};
            ac.actorId = GetPhysicsActorId(m_world, e);
            m_pullContexts.push_back(ac);
        }

        m_physics->PullProxyStates(m_pullContexts);

        for (auto& ac : m_pullContexts)
        {
            const entt::entity e = (*nwPtr)->ResolveActor(ActorId(ac.actorId));
            if (!m_world.valid(e) || !m_world.all_of<ActorBodyType>(e))
                continue;

            const auto bodyType = m_world.get<ActorBodyType>(e).body;

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
        const entt::entity local = GetCachedLocalEntity(m_world);
        if (local == entt::null || !m_world.valid(local)) return;

        const px::ActorId physicsActorId = GetPhysicsActorId(m_world, local);
        auto& inputHistory = m_world.ctx().get<InputHistoryBuffer>();
        auto& replayBuf    = m_world.ctx().get<ReplayPredictedBuffer>();
        auto& replayStats  = m_world.ctx().get<ReplayStats>();

        const uint32 currentSeq = inputHistory.current.sequence;

        replayStats = {};

        uint32 replayCandidateCount = 0;
        uint32 replayStepCount = 0;
        uint32 replayMeaningfulInputCount = 0;
        uint32 firstReplaySeq = 0;
        uint32 lastReplaySeq = 0;

        inputHistory.ForEachReplayRange(ctx.inputAck, currentSeq,
            [this, local, physicsActorId, &replayCandidateCount, &replayStepCount, &replayMeaningfulInputCount, &firstReplaySeq, &lastReplaySeq, &replayBuf, &ctx](const CharacterControlCommand& cmd)
            {
                ++replayCandidateCount;

                if (replayStepCount >= m_config.maxReplayInputs)
                    return;

                if (HasMeaningfulReplayInput(cmd.intent))
                    ++replayMeaningfulInputCount;

                if (firstReplaySeq == 0)
                    firstReplaySeq = cmd.sequence;
                lastReplaySeq = cmd.sequence;

                ApplyInput(cmd, true);

                ReplayContext step{ .tick = cmd.sequence, .local = local, .inputAck = ctx.inputAck };
                m_replayRunner->Run(m_world, step);
                Resimulate();

                px::CharacterState cs{};
                m_physics->PullCorrectionState(physicsActorId, cs);
                replayBuf.Push(cmd.sequence, cs);

                ++replayStepCount;
            });

        replayStats.stepCount            = replayStepCount;
        replayStats.meaningfulInputCount = replayMeaningfulInputCount;
        replayStats.truncated            = replayCandidateCount > replayStepCount;

        //JAMNET_LOG_DEBUG(
        //    "[ClientReplay] serverTick={}, inputAck={}, currentSeq={}, firstReplaySeq={}, lastReplaySeq={}, candidates={}, steps={}, meaningful={}, truncated={}",
        //    ctx.tick,
        //    ctx.inputAck,
        //    currentSeq,
        //    firstReplaySeq,
        //    lastReplaySeq,
        //    replayCandidateCount,
        //    replayStepCount,
        //    replayMeaningfulInputCount,
        //    replayStats.truncated);
    }


	void ClientPhysicsSystem::ApplyInput(const CharacterControlCommand& cmd, bool useReplayState)
    {
        entt::entity player = GetCachedLocalEntity(m_world);
        if (player == entt::null || !m_world.valid(player) || !m_physics)
            return;

        const px::ActorId id = GetPhysicsActorId(m_world, player);
        const auto* selfState = useReplayState
            ? ResolveReplayCharacterState(m_world, player)
            : ResolveCharacterState(m_world, player);
		const px::CharacterMotorInput resolvedInput = ResolveInputForSimulation(cmd.intent, selfState, useReplayState);
        if (useReplayState)
            m_physics->ApplyReplayCharacterMotorInput(id, resolvedInput);
        else
            m_physics->ApplyCharacterMotorInput(id, resolvedInput);
    }

	px::CharacterMotorInput ClientPhysicsSystem::ResolveInputForSimulation(const CharacterControlIntent& intent, const px::CharacterState* selfState, bool useReplayState) const
	{
		CharacterControlResolveContext context{};
		context.selfState = selfState;
		if (const auto* follow = std::get_if<FollowActorIntent>(&intent.locomotion))
		{
			context.hasFollowTargetPosition = TryResolveTargetPos(
				follow->target.Value(),
				context.followTargetPosition,
				useReplayState);
		}

		return CharacterControlResolver::Resolve(intent, context, m_controlResolveConfig);
    }

    bool ClientPhysicsSystem::TryResolveTargetPos(uint32 targetActorIdRaw, OUT px::Vec3& outPos, bool useReplayState) const
    {
        if (targetActorIdRaw == 0)
            return false;

        const ActorId targetActorId = ActorId(targetActorIdRaw);
        if (auto* nwPtr = m_world.ctx().find<ClientWorld*>(); nwPtr && *nwPtr)
        {
            const entt::entity e = (*nwPtr)->ResolveActor(targetActorId);
            if (e != entt::null && m_world.valid(e))
            {
                return useReplayState
                    ? ResolveReplayActorTargetPos(m_world, e, outPos)
                    : ResolveActorTargetPos(m_world, e, outPos);
            }
        }

        return false;
    }


    void ClientPhysicsSystem::Simulate()
    {
        if (!m_physics) return;

        auto& shard = CurrentShardLocalChecked();
        auto* sched = shard.scheduler;

        const bool   inFiber  = sched && (sched->Current() != 0);
        auto* executor = static_cast<ShardExecutor*>(CurrentExecutor());
        const uint64 awaitKey = inFiber && executor ? executor->AllocateAwaitKey() : 0;

        if (m_physics->BeginSimulate(SIMULATION_TICK_SEC, awaitKey) && inFiber)
            sched->Suspend(awaitKey, NOW_NS() + 1_s);

        m_physics->EndSimulate();
        CommitPendingActorOps();
    }

    void ClientPhysicsSystem::Resimulate()
    {
        if (!m_physics) return;

        auto& shard = CurrentShardLocalChecked();
        auto* sched = shard.scheduler;

        const bool   inFiber  = sched && (sched->Current() != 0);
        auto* executor = static_cast<ShardExecutor*>(CurrentExecutor());
        const uint64 awaitKey = inFiber && executor ? executor->AllocateAwaitKey() : 0;

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
				const px::ActorId physicsActorId = GetPhysicsActorId(m_world, op.e);
				if (m_physics->GetBodyType(physicsActorId) == px::eBodyType::None)
				{
					if (auto* clientWorld = m_world.ctx().find<ClientWorld*>(); clientWorld && *clientWorld)
						(*clientWorld)->RollbackPhysicsSpawn(op.e);
					continue;
				}

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

        auto* nwPtr = m_world.ctx().find<ClientWorld*>();
        if (!nwPtr || !*nwPtr)
            return;

        ClientWorld* physicalWorld = *nwPtr;

        for (const px::PhysicsEvent& evt : m_physics->ConsumePhysicsEvents())
        {
            if (evt.type != px::ePhysicsEventType::ProjectileLifetimeExpired)
                continue;

            const entt::entity e = physicalWorld->ResolveActor(ActorId(evt.sourceActorId));
            if (e == entt::null || !m_world.valid(e))
                continue;

            const auto* owner = m_world.try_get<OwnershipTag>(e);
            const auto* actorId = m_world.try_get<ActorId>(e);
            if (!owner || !actorId || !actorId->IsValid())
                continue;

            if (owner->userId == 0 || owner->userId != m_userId)
                continue;

            physicalWorld->HideReplicatedActorUntilConfirmed(*actorId);
        }
    }
}
