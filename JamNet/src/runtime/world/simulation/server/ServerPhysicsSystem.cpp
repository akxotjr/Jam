#include "pch.h"
#include "jamnet/runtime/world/simulation/server/ServerPhysicsSystem.h"

#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/core/executor/ShardExecutor.h"
#include "jamnet/runtime/world/simulation/common/ActorComponents.h"
#include "jamnet/runtime/world/simulation/server/ServerInputSystem.h"
#include "jamnet/runtime/world/simulation/server/ServerWorld.h"
#include "jamnet/runtime/world/simulation/common/WorldContext.h"

#include "jampx/PhysicsFacade.h"


namespace jam::net
{
	namespace
	{
		bool TryReadValidatedCharacterState(const px::PhysicsFacade* physics, px::ActorId actorId, OUT px::CharacterState& outState)
		{
			if (!physics->GetCharacterState(actorId, outState))
				return false;

			const bool finite = outState.IsFinite();
			JAM_ASSERT(finite && "Physics readback produced non-finite CharacterState");
			return finite;
		}

		bool TryReadValidatedRigidState(const px::PhysicsFacade* physics, px::ActorId actorId, OUT px::RigidState& outState)
		{
			if (!physics->GetRigidState(actorId, outState))
				return false;

			outState.pose.q.Normalize();

			const bool finite = outState.IsFinite();
			JAM_ASSERT(finite && "Physics readback produced non-finite RigidState");
			return finite;
		}
	}

	ServerPhysicsSystem::ServerPhysicsSystem(entt::registry& world, px::PhysicsFacade* physics, ServerWorld& serverWorld, ServerInputSystem& inputSystem)
		: m_world(world), m_physics(physics), m_serverWorld(serverWorld), m_inputSystem(inputSystem)
	{
	}

	void ServerPhysicsSystem::Init()
	{
		m_completedTickCount = 0;
		m_tickFiberRunning = false;
		m_lastActiveEntities.clear();
	}

	void ServerPhysicsSystem::Tick()
	{
		if (!m_physics) return;

		m_tickFiberRunning = true;
		try
		{
			ApplyInputs();
			Simulate();
			SyncActiveTransforms();
			HandlePhysicsEvents();
			++m_completedTickCount;
		}
		catch (...)
		{
			m_tickFiberRunning = false;
			throw;
		}
		m_tickFiberRunning = false;
	}

	void ServerPhysicsSystem::SpawnActor(entt::entity e, const px::SpawnDesc& desc) const
	{
		if (!m_physics || !m_world.valid(e))
			return;

		const bool isSafePoint = !m_physics->IsStepPending();
		JAM_ASSERT(isSafePoint && "Server actor spawn must run at the World pipeline safe point");
		if (!isSafePoint)
			return;

		const px::ActorId physicsActorId = GetPhysicsActorId(m_world, e);
		if (!m_physics->Spawn(physicsActorId, desc))
			return;

		m_world.emplace<PhysicsSpawnedTag>(e);

		if (auto* cs = m_world.try_get<CharAuthorityState>(e))
		{
			if (!TryReadValidatedCharacterState(m_physics, physicsActorId, cs->state))
			{
				m_world.remove<PhysicsSpawnedTag>(e);
				m_physics->Despawn(physicsActorId);
				return;
			}
		}
		else if (auto* rs = m_world.try_get<RigidAuthorityState>(e))
		{
			if (!TryReadValidatedRigidState(m_physics, physicsActorId, rs->state))
			{
				m_world.remove<PhysicsSpawnedTag>(e);
				m_physics->Despawn(physicsActorId);
				return;
			}
		}
	}

	void ServerPhysicsSystem::DespawnActor(entt::entity e) const
	{
		if (!m_physics || !m_world.valid(e))
			return;

		const bool isSafePoint = !m_physics->IsStepPending();
		JAM_ASSERT(isSafePoint && "Server actor despawn must run at the World pipeline safe point");
		if (!isSafePoint)
			return;

		const px::ActorId id = GetPhysicsActorId(m_world, e);
		if (!m_physics->Despawn(id))
			return;

		m_world.erase<PhysicsSpawnedTag>(e);
	}

	void ServerPhysicsSystem::ApplyInputs() const
	{
		auto view = m_world.view<ControlTag, px::CharacterMotorInput>();
		for (auto e : view)
		{
			const auto& control = view.get<ControlTag>(e);
			auto& input = view.get<px::CharacterMotorInput>(e);
			m_physics->ApplyCharacterMotorInput(GetPhysicsActorId(m_world, e), input);

			if (control.userId != 0)
				m_inputSystem.MarkInputApplied(control.userId);
		}
	}

	void ServerPhysicsSystem::Simulate()
	{
		if (!m_physics) return;
		
		auto& shard = CurrentShardLocalChecked();
		auto* sched = shard.scheduler;

		const bool inFiber = sched && (sched->Current() != 0);

		auto* executor = static_cast<ShardExecutor*>(CurrentExecutor());
		const uint64 awaitKey = inFiber && executor ? executor->AllocateAwaitKey() : 0;

		if (m_physics->BeginSimulate(SIMULATION_TICK_SEC, awaitKey) && inFiber)
		{
			sched->Suspend(awaitKey, NOW_NS() + 2_s);
		}

		m_physics->EndSimulate();
	}

	void ServerPhysicsSystem::SyncActiveTransforms() const
	{
		if (!m_physics) return;

		m_world.clear<ReplicationActiveTag>();
		m_lastActiveEntities.clear();

		// PopActiveList = onAdvance(dynamic) + dirty set(kinematic / character move / setGlobalPose)
		const auto activeList = m_physics->PopActiveList();
		if (activeList.empty()) return;

		px::CharacterState csBuf{};
		px::RigidState	   rsBuf{};

		for (const px::ActorId id : activeList)
		{
			const entt::entity e = m_serverWorld.ResolveActor(ActorId(id));
			if (e == entt::null || !m_world.valid(e)) continue;

			if (auto* cs = m_world.try_get<CharAuthorityState>(e))
			{
				if (TryReadValidatedCharacterState(m_physics, id, csBuf))
				{
					cs->state = csBuf;
					m_world.emplace<ReplicationActiveTag>(e);
					m_lastActiveEntities.push_back(e);
				}
			}
			else if (auto* rs = m_world.try_get<RigidAuthorityState>(e))
			{
				if (TryReadValidatedRigidState(m_physics, id, rsBuf))
				{
					rs->state = rsBuf;
					m_world.emplace<ReplicationActiveTag>(e);
					m_lastActiveEntities.push_back(e);
				}
			}
		}
	}

	void ServerPhysicsSystem::SyncTransforms() const
	{
		if (!m_physics) return;

		auto view = m_world.view<ActorId, ActorBodyType, PhysicsSpawnedTag>();

		for (auto e : view)
		{
			const px::ActorId physicsActorId = GetPhysicsActorId(m_world, e);
			const auto bodyType = view.get<ActorBodyType>(e).body;

			if (bodyType == px::eBodyType::Character)
			{
				px::CharacterState cs{};
				if (TryReadValidatedCharacterState(m_physics, physicsActorId, cs))
				{
					if (auto* state = m_world.try_get<CharAuthorityState>(e))
						state->state = cs;
				}
			}
			else
			{
				px::RigidState rs{};
				if (TryReadValidatedRigidState(m_physics, physicsActorId, rs))
				{
					if (auto* state = m_world.try_get<RigidAuthorityState>(e))
						state->state = rs;
				}
			}
		}
	}

	void ServerPhysicsSystem::HandlePhysicsEvents() const
	{
		if (!m_physics)
			return;

		std::unordered_set<uint32> pending;
		const std::vector<px::PhysicsEvent> events = m_physics->ConsumePhysicsEvents();

		for (const px::PhysicsEvent& evt : events)
		{
			if (evt.type != px::ePhysicsEventType::ProjectileHit
				&& evt.type != px::ePhysicsEventType::ProjectileLifetimeExpired)
			{
				continue;
			}

			const entt::entity e = m_serverWorld.ResolveActor(ActorId(evt.sourceActorId));
			if (e == entt::null || !m_world.valid(e))
				continue;

			const auto* actorId = m_world.try_get<ActorId>(e);
			if (!actorId || !actorId->IsValid())
				continue;

			if (!pending.insert(actorId->Value()).second)
				continue;

			m_serverWorld.DespawnActor(*actorId);
		}

		m_serverWorld.DispatchPhysicsEvents(events);
	}
}
