#include "pch.h"
#include "jamnet/runtime/world/simulation/server/ServerPhysicsSystem.h"

#include "jamnet/runtime/world/simulation/common/ActorComponents.h"
#include "jamnet/runtime/world/simulation/server/ServerWorld.h"
#include "jamnet/runtime/world/simulation/common/WorldContext.h"

#include "jampx/PhysicsFacade.h"

#include <type_traits>

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

	ServerPhysicsSystem::ServerPhysicsSystem(ServerWorld& world, entt::registry& registry, px::PhysicsFacade* physics)
		: m_world(world), m_registry(registry), m_physics(physics)
	{
	}

	void ServerPhysicsSystem::Init()
	{
		m_completedTickCount = 0;
		m_lastActiveEntities.clear();
		m_lastAppliedInputUsers.clear();
	}

	void ServerPhysicsSystem::Tick()
	{
		m_lastAppliedInputUsers.clear();
		m_lastActiveEntities.clear();
		if (!m_physics) return;

		ApplyInputs();
		Simulate();
		SyncActiveTransforms();
		HandlePhysicsEvents();
		++m_completedTickCount;
	}

	bool ServerPhysicsSystem::SpawnActor(entt::entity e, const px::SpawnDesc& desc) const
	{
		const px::ActorId physicsActorId = GetPhysicsActorId(m_registry, e);
		if (!m_physics->Spawn(physicsActorId, desc))
			return false;

		m_registry.emplace<PhysicsSpawnedTag>(e);

		if (auto* cs = m_registry.try_get<CharAuthorityState>(e))
		{
			if (!TryReadValidatedCharacterState(m_physics, physicsActorId, cs->state))
			{
				m_registry.remove<PhysicsSpawnedTag>(e);
				m_physics->Despawn(physicsActorId);
				return false;
			}
		}
		else if (auto* rs = m_registry.try_get<RigidAuthorityState>(e))
		{
			if (!TryReadValidatedRigidState(m_physics, physicsActorId, rs->state))
			{
				m_registry.remove<PhysicsSpawnedTag>(e);
				m_physics->Despawn(physicsActorId);
				return false;
			}
		}
		return true;
	}

	bool ServerPhysicsSystem::DespawnActor(entt::entity e) const
	{
		if (!m_physics || !m_registry.valid(e))
			return false;

		if (!m_registry.all_of<PhysicsSpawnedTag>(e))
			return true;

		const bool isSafePoint = !m_physics->IsStepPending();
		JAM_ASSERT(isSafePoint && "Server actor despawn must run at the World pipeline safe point");
		if (!isSafePoint)
			return false;

		const px::ActorId id = GetPhysicsActorId(m_registry, e);
		if (!m_physics->Despawn(id))
			return false;

		m_registry.erase<PhysicsSpawnedTag>(e);
		return true;
	}

	void ServerPhysicsSystem::ApplyInputs()
	{
		auto view = m_registry.view<ControlTag, px::CharacterMotorInput>();
		for (auto e : view)
		{
			const auto& control = view.get<ControlTag>(e);
			auto& input = view.get<px::CharacterMotorInput>(e);
			m_physics->ApplyCharacterMotorInput(GetPhysicsActorId(m_registry, e), input);

			if (control.userId != 0)
				m_lastAppliedInputUsers.push_back(control.userId);
		}
	}

	void ServerPhysicsSystem::Simulate()
	{
		if (!m_physics) return;
		m_physics->Simulate(SIMULATION_TICK_SEC);
	}

	void ServerPhysicsSystem::SyncActiveTransforms()
	{
		if (!m_physics) return;

		m_registry.clear<ReplicationActiveTag>();
		m_lastActiveEntities.clear();

		// PopActiveList = onAdvance(dynamic) + dirty set(kinematic / character move / setGlobalPose)
		const auto activeList = m_physics->PopActiveList();
		if (activeList.empty()) return;

		px::CharacterState csBuf{};
		px::RigidState	   rsBuf{};

		for (const px::ActorId id : activeList)
		{
			const entt::entity e = m_world.ResolveActor(ActorId(id));
			if (e == entt::null || !m_registry.valid(e)) continue;

			if (auto* cs = m_registry.try_get<CharAuthorityState>(e))
			{
				if (TryReadValidatedCharacterState(m_physics, id, csBuf))
				{
					cs->state = csBuf;
					m_registry.emplace<ReplicationActiveTag>(e);
					m_lastActiveEntities.push_back(e);
				}
			}
			else if (auto* rs = m_registry.try_get<RigidAuthorityState>(e))
			{
				if (TryReadValidatedRigidState(m_physics, id, rsBuf))
				{
					rs->state = rsBuf;
					m_registry.emplace<ReplicationActiveTag>(e);
					m_lastActiveEntities.push_back(e);
				}
			}
		}
	}

	void ServerPhysicsSystem::SyncTransforms() const
	{
		if (!m_physics) return;

		auto view = m_registry.view<ActorId, ActorBodyType, PhysicsSpawnedTag>();

		for (auto e : view)
		{
			const px::ActorId physicsActorId = GetPhysicsActorId(m_registry, e);
			const auto bodyType = view.get<ActorBodyType>(e).body;

			if (bodyType == px::eBodyType::Character)
			{
				px::CharacterState cs{};
				if (TryReadValidatedCharacterState(m_physics, physicsActorId, cs))
				{
					if (auto* state = m_registry.try_get<CharAuthorityState>(e))
						state->state = cs;
				}
			}
			else
			{
				px::RigidState rs{};
				if (TryReadValidatedRigidState(m_physics, physicsActorId, rs))
				{
					if (auto* state = m_registry.try_get<RigidAuthorityState>(e))
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

			const entt::entity e = m_world.ResolveActor(ActorId(evt.sourceActorId));
			if (e == entt::null || !m_registry.valid(e))
				continue;

			const auto* actorId = m_registry.try_get<ActorId>(e);
			if (!actorId || !actorId->IsValid())
				continue;

			if (!pending.insert(actorId->Value()).second)
				continue;

			m_world.Post(Job([world = &m_world, actorId = *actorId]()
				{
					world->DespawnActorAsync(kInvalidUserId, actorId);
				}));
		}

		m_world.DispatchPhysicsEvents(events);
	}
}
