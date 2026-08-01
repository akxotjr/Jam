#include "pch.h"
#include "jamnet/runtime/world/simulation/common/PhysicalWorld.h"
#include "jamnet/runtime/world/simulation/common/ShardJobBridge.h"
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

	void PhysicalWorld::StartTickPipeline(ShardDomain domain, uint64 dt_ns)
	{
		auto& local = CurrentShardLocalChecked();
		auto* scheduler = local.scheduler;
		auto shard = m_shard.lock();
		if (!scheduler || !shard)
			throw std::runtime_error("PhysicalWorld tick pipeline requires a fiber scheduler");

		m_pipelineDomain = domain;
		m_pipelineScheduler = scheduler;
		m_pipelineExecutor = shard.get();
		m_pipelineActive = true;

		RegisterShardSystemFn(local, domain, dt_ns,
			[this](ShardLocal&, uint64, uint64)
			{
				RequestPipelineTick();
			});

		if (m_pipelineFiberId == 0)
		{
			m_pipelineFiberId = scheduler->SpawnFiber(
				[this]
				{
					RunTickPipeline();
				},
				FiberDesc{ .name = "PhysicalWorld.TickPipeline" });
		}
		else if (m_pipelineIdleAwaitKey != 0)
		{
			if (scheduler->Resume(m_pipelineIdleAwaitKey))
				m_pipelineIdleAwaitKey = 0;
		}
	}

	void PhysicalWorld::StopTickPipeline()
	{
		auto& local = CurrentShardLocalChecked();
		local.domainGroups.erase(m_pipelineDomain);

		m_pipelineActive = false;
		m_pipelineTickDebt = 0;

		if (m_pipelineScheduler && m_pipelineIdleAwaitKey != 0)
		{
			if (m_pipelineScheduler->Resume(m_pipelineIdleAwaitKey))
				m_pipelineIdleAwaitKey = 0;
		}
	}

	void PhysicalWorld::ShutdownPhysicsWhenPipelineStops()
	{
		if (!m_physics)
			return;

		if (m_pipelineFiberId == 0)
		{
			m_physics->Shutdown();
			return;
		}

		m_shutdownPhysicsOnPipelineStop = true;
	}

	bool PhysicalWorld::DeferUntilPipelineSafePoint(std::function<void()> mutation)
	{
		if (!mutation || !m_pipelineActive || !m_pipelineTickInProgress)
			return false;

		m_pendingPipelineMutations.push_back(std::move(mutation));
		return true;
	}

	void PhysicalWorld::OnShutdownBarrier(std::function<void()> completion)
	{
		if (!completion)
			return;

		JAM_ASSERT(!m_pipelineShutdownCompletion);
		if (m_pipelineFiberId != 0)
		{
			m_pipelineShutdownCompletion = std::move(completion);
			return;
		}

		if (auto shard = m_shard.lock())
			shard->Submit(Job(std::move(completion), eJobPriority::Control));
		else
			completion();
	}

	void PhysicalWorld::RequestPipelineTick()
	{
		if (!m_pipelineActive)
			return;

		if (m_pipelineTickDebt < m_pipelineTickDebtCap)
			++m_pipelineTickDebt;

		if (m_pipelineScheduler && m_pipelineIdleAwaitKey != 0)
		{
			if (m_pipelineScheduler->Resume(m_pipelineIdleAwaitKey))
				m_pipelineIdleAwaitKey = 0;
		}
	}

	void PhysicalWorld::RunTickPipeline()
	{
		try
		{
			while (m_pipelineActive)
			{
				if (m_pipelineTickDebt == 0)
				{
					m_pipelineIdleAwaitKey = m_pipelineExecutor->AllocateAwaitKey();
					m_pipelineScheduler->Suspend(m_pipelineIdleAwaitKey, 0);
					m_pipelineIdleAwaitKey = 0;
					continue;
				}

				uint32 burst = 0;
				while (m_pipelineActive
					&& m_pipelineTickDebt > 0
					&& burst < m_pipelineTickBurstBudget)
				{
					--m_pipelineTickDebt;
					m_pipelineTickInProgress = true;
					try
					{
						Tick();
					}
					catch (...)
					{
						m_pipelineTickInProgress = false;
						throw;
					}
					m_pipelineTickInProgress = false;
					DrainPipelineMutations();
					++burst;
				}

				m_pipelineLastBurstCount = burst;
				if (m_pipelineActive && m_pipelineTickDebt > 0)
					m_pipelineScheduler->YieldFiber();
			}
		}
		catch (...)
		{
			FinishTickPipeline();
			throw;
		}

		FinishTickPipeline();
	}

	void PhysicalWorld::DrainPipelineMutations()
	{
		while (!m_pendingPipelineMutations.empty())
		{
			auto mutations = std::move(m_pendingPipelineMutations);
			m_pendingPipelineMutations.clear();

			for (auto& mutation : mutations)
				mutation();
		}
	}

	void PhysicalWorld::FinishTickPipeline()
	{
		ShardExecutor* pipelineExecutor = m_pipelineExecutor;
		m_pipelineIdleAwaitKey = 0;
		m_pipelineFiberId = 0;
		m_pipelineTickDebt = 0;
		m_pipelineLastBurstCount = 0;
		m_pipelineTickInProgress = false;
		m_pipelineScheduler = nullptr;
		m_pipelineExecutor = nullptr;

		if (m_shutdownPhysicsOnPipelineStop && m_physics)
		{
			m_shutdownPhysicsOnPipelineStop = false;
			m_physics->Shutdown();
		}

		if (m_pipelineShutdownCompletion)
		{
			auto completion = std::move(m_pipelineShutdownCompletion);
			if (pipelineExecutor)
				pipelineExecutor->Submit(Job(std::move(completion), eJobPriority::Control));
			else
				completion();
		}
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
