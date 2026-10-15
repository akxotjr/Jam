#include "pch.h"
#include "jamnet/runtime/world/simulation/common/PhysicalWorld.h"
#include "jamnet/core/executor/ShardExecutor.h"
#include "jamnet/core/executor/ThreadContext.h"

#include <jampx/PhysicsFacade.h>


#include <unordered_set>

namespace jam::net
{
	PhysicalWorld::PhysicalWorld() = default;

	PhysicalWorld::PhysicalWorld(const WorldConfig& config)
		: WorldMembershipHost(config), m_physics(std::make_unique<px::PhysicsFacade>())
	{
		m_physics->SetPhysicsAssetPath(config.physicsAssetPath);
	}

	PhysicalWorld::~PhysicalWorld() = default;

	void PhysicalWorld::SetRuntimeState(eWorldRuntimeState runtime)
	{
		if (m_runtimeState == runtime)
			return;

		const eWorldRuntimeState previous = m_runtimeState;
		m_runtimeState = runtime;

		if (runtime == eWorldRuntimeState::Active)
		{
			if (previous == eWorldRuntimeState::Paused)
			{
				Resume(m_tickIntervalNs);
				return;
			}

			if (previous != eWorldRuntimeState::Active)
				Start(m_tickIntervalNs);
			return;
		}

		if (previous == eWorldRuntimeState::Active)
			Stop();
	}

	void PhysicalWorld::StartDomainTick(ShardDomain domain, Duration dt)
	{
		JAM_ASSERT(IsCurrentShardContext());
		JAM_ASSERT(!m_tickInProgress);
		if (m_tickActive)
			StopDomainTick();

		m_tickDomain = domain;
		m_tickActive = true;
		RegisterShardSystemFn(CurrentShardLocalChecked(), domain, dt,
			[this](ShardLocal&, Timestamp, Duration)
			{
				if (!m_tickActive || !m_alive.load(std::memory_order_acquire))
					return;

				JAM_ASSERT(!m_tickInProgress);
				struct TickScope
				{
					bool& inProgress;
					~TickScope() { inProgress = false; }
				};
				m_tickInProgress = true;
				TickScope scope{ m_tickInProgress };
				Tick();
			});
	}

	void PhysicalWorld::StopDomainTick()
	{
		JAM_ASSERT(IsCurrentShardContext());
		if (!m_tickActive)
			return;

		m_tickActive = false;
		CurrentShardLocalChecked().domainGroups.erase(m_tickDomain);
	}

	void PhysicalWorld::ShutdownPhysicsAfterTick()
	{
		if (!m_physics)
			return;

		if (m_tickInProgress)
		{
			if (auto shard = m_shard.lock())
			{
				shard->Submit(Job([this] { m_physics->Shutdown(); }, eJobPriority::Control));
				return;
			}
		}
		m_physics->Shutdown();
	}

	void PhysicalWorld::BeginCloseBarrier(std::function<void()> completion)
	{
		if (!completion)
			return;

		// Finalization must run after a domain tick and its callback have returned.
		if (auto shard = m_shard.lock())
			shard->Submit(Job(std::move(completion), eJobPriority::Control));
		else
			completion();
	}

	ActorId PhysicalWorld::AllocateActorId(entt::entity entity)
	{
		const ActorId actorId = m_actorDirectory.Allocate(entity);
		if (!actorId.IsValid())
			return ActorId::Invalid();

		m_registry.emplace<ActorId>(entity, actorId);
		return actorId;
	}

	bool PhysicalWorld::ReleaseActorId(entt::entity entity)
	{
		if (!m_registry.valid(entity))
			return false;

		const auto* actorId = m_registry.try_get<ActorId>(entity);
		if (!actorId)
			return false;

		const bool released = m_actorDirectory.Release(*actorId)
			|| m_actorDirectory.Unbind(*actorId);
		m_registry.remove<ActorId>(entity);
		return released;
	}

	ActorId PhysicalWorld::GetActorId(entt::entity entity) const
	{
		if (!m_registry.valid(entity))
			return ActorId::Invalid();

		const auto* actorId = m_registry.try_get<ActorId>(entity);
		return actorId ? *actorId : ActorId::Invalid();
	}

	entt::entity PhysicalWorld::ResolveActor(ActorId actorId) const
	{
		const entt::entity entity = m_actorDirectory.Resolve(actorId);
		return (entity != entt::null && m_registry.valid(entity)) ? entity : entt::null;
	}

	bool PhysicalWorld::ValidateActorIdentities() const
	{
		if (!m_actorDirectory.Validate())
			return false;

		std::unordered_set<ActorId> actorIds;
		for (const entt::entity entity : m_registry.view<ActorId>())
		{
			const ActorId actorId = m_registry.get<ActorId>(entity);
			if (!actorId.IsValid() || !actorIds.insert(actorId).second || m_actorDirectory.Resolve(actorId) != entity)
				return false;
		}
		return true;
	}

	void PhysicalWorld::ClearActorDirectory()
	{
		m_actorDirectory.Clear();
	}
}
