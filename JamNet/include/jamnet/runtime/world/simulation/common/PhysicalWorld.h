#pragma once

#include "jamnet/runtime/world/actor/ActorArchetypeDatabase.h"
#include "jamnet/runtime/world/actor/ActorDirectory.h"
#include "jamnet/runtime/world/data/ActorLevelDatabase.h"
#include "jamnet/runtime/world/lifecycle/WorldBase.h"
#include "jamnet/runtime/world/lifecycle/WorldTransitionTypes.h"
#include "jamnet/runtime/world/simulation/common/ReplicationTypes.h"
#include "jamnet/runtime/world/simulation/common/ActorComponents.h"
#include "jamnet/core/executor/FiberCommon.h"
#include "jamnet/core/executor/ShardDomain.h"

#include <jampx/PhysicsTypes.h>

#include <memory>
#include <functional>
#include <utility>
#include <vector>

namespace jam::px
{
	class PhysicsFacade;
}

namespace jam
{
	class FiberScheduler;
	class ShardExecutor;
}

namespace jam::net
{
	class ShardJobBridge;

	struct SpawnParams
	{
		// JamNet authoring/gameplay layer key.
		ActorArchetypeKey		actorArchetypeKey{};
		// JamPx execution layer key lives in desc.archetype.
		px::SpawnDesc			desc{};

		ClientRequestId			clientRequestId = kInvalidClientRequestId;

		bool					owned = true;
		bool					controlled = false;

		uint64					owner = 0;
		uint64					controller = 0;

		ActorId					targetActorId = ActorId::Invalid();
		WorldEventCorrelation	correlation{};
	};

	class PhysicalWorld : public WorldMembershipHost
	{
	public:
		PhysicalWorld();
		PhysicalWorld(const WorldConfig& config);
		~PhysicalWorld() override;

		virtual void						Start(uint64 dt_ns) = 0;
		virtual void						Resume(uint64 dt_ns) = 0;
		virtual void						Stop() = 0;
		void								SetRuntimeState(eWorldRuntimeState runtime);
		eWorldRuntimeState					GetRuntimeState() const { return m_runtimeState; }
		void								SetTickIntervalNs(uint64 dt_ns) { m_tickIntervalNs = dt_ns; }
		void								SetActorArchetypeDatabase(ActorArchetypeDatabase database) { m_actorArchetypes = std::move(database); }
		void								SetActorLevelDatabase(ActorLevelDatabase database) { m_actorLevels = std::move(database); }
		uint32								GetPipelineTickDebt() const { return m_pipelineTickDebt; }
		uint32								GetPipelineLastBurstCount() const { return m_pipelineLastBurstCount; }
		bool								IsPipelineFiberRunning() const { return m_pipelineFiberId != 0; }

		entt::registry&						GetRegistry() { return m_registry; }
		ActorId								GetActorId(entt::entity entity) const;
		entt::entity						ResolveActor(ActorId actorId) const;
		bool								ValidateActorIdentities() const;

	protected:
		ActorId								AllocateActorId(entt::entity entity);
		bool								ReleaseActorId(entt::entity entity);
		void								ClearActorDirectory();
		void								StartTickPipeline(ShardDomain domain, uint64 dt_ns);
		void								StopTickPipeline();
		void								ShutdownPhysicsWhenPipelineStops();
		bool								IsPipelineTickInProgress() const { return m_pipelineActive && m_pipelineTickInProgress; }
		bool								DeferUntilPipelineSafePoint(std::function<void()> mutation);
		void								OnShutdownBarrier(std::function<void()> completion) override;

		virtual void						Tick() = 0;

	private:
		void								RequestPipelineTick();
		void								RunTickPipeline();
		void								DrainPipelineMutations();
		void								FinishTickPipeline();

	protected:
		entt::registry						m_registry;
		ActorDirectory						m_actorDirectory;
		std::unique_ptr<ShardJobBridge>		m_bridge;
		std::unique_ptr<px::PhysicsFacade>	m_physics;
		ActorArchetypeDatabase				m_actorArchetypes;
		ActorLevelDatabase					m_actorLevels;

		eWorldRuntimeState					m_runtimeState	 = eWorldRuntimeState::Standby;
		uint64								m_tickIntervalNs = SIMULATION_TICK_NS;

	private:
		FiberScheduler*						m_pipelineScheduler = nullptr;
		ShardExecutor*						m_pipelineExecutor = nullptr;
		ShardDomain							m_pipelineDomain = {};
		FiberAwaitKey						m_pipelineIdleAwaitKey = 0;
		uint32								m_pipelineFiberId = 0;
		uint32								m_pipelineTickDebt = 0;
		uint32								m_pipelineTickDebtCap = 8;
		uint32								m_pipelineTickBurstBudget = 1;
		uint32								m_pipelineLastBurstCount = 0;
		bool								m_pipelineActive = false;
		bool								m_pipelineTickInProgress = false;
		bool								m_shutdownPhysicsOnPipelineStop = false;
		std::vector<std::function<void()>>	m_pendingPipelineMutations;
		std::function<void()>				m_pipelineShutdownCompletion;
	};
}
