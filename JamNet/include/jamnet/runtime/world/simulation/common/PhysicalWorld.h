#pragma once

#include "jamnet/runtime/world/actor/ActorArchetypeDatabase.h"
#include "jamnet/runtime/world/actor/ActorDirectory.h"
#include "jamnet/runtime/world/data/ActorLevelDatabase.h"
#include "jamnet/runtime/world/lifecycle/WorldBase.h"
#include "jamnet/runtime/world/lifecycle/WorldTransitionTypes.h"
#include "jamnet/runtime/world/simulation/common/ReplicationTypes.h"
#include "jamnet/runtime/world/simulation/common/ActorComponents.h"
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

namespace jam::net
{

	struct SpawnParams
	{
		// JamNet authoring/gameplay layer key.
		ActorArchetypeKey		actorArchetypeKey{};
		// JamPx execution layer key lives in desc.archetype.
		px::SpawnDesc			desc{};

		ClientRequestId			clientRequestId = kInvalidClientRequestId;

		uint64					owner = 0;
		uint64					controller = 0;

		ActorId					targetActorId = ActorId::Invalid();
	};

	class PhysicalWorld : public WorldMembershipHost
	{
	public:
		PhysicalWorld();
		PhysicalWorld(const WorldConfig& config);
		~PhysicalWorld() override;

		virtual void						Start(Duration dt) = 0;
		virtual void						Resume(Duration dt) = 0;
		virtual void						Stop() = 0;
		void								SetRuntimeState(eWorldRuntimeState runtime);
		eWorldRuntimeState					GetRuntimeState() const { return m_runtimeState; }
		void								SetTickIntervalNs(Duration dt) { m_tickIntervalNs = dt; }
		void								SetActorArchetypeDatabase(ActorArchetypeDatabase database) { m_actorArchetypes = std::move(database); }
		void								SetActorLevelDatabase(ActorLevelDatabase database) { m_actorLevels = std::move(database); }

		entt::registry&						GetRegistry() { return m_registry; }
		ActorId								GetActorId(entt::entity entity) const;
		entt::entity						ResolveActor(ActorId actorId) const;
		bool								ValidateActorIdentities() const;

	protected:
		ActorId								AllocateActorId(entt::entity entity);
		bool								ReleaseActorId(entt::entity entity);
		void								ClearActorDirectory();
		void								StartDomainTick(ShardDomain domain, Duration dt);
		void								StopDomainTick();
		void								ShutdownPhysicsAfterTick();
		bool								IsTickInProgress() const { return m_tickInProgress; }
		void								BeginCloseBarrier(std::function<void()> completion) override;

		virtual void						Tick() = 0;

	protected:
		entt::registry						m_registry;
		ActorDirectory						m_actorDirectory;
		std::unique_ptr<px::PhysicsFacade>	m_physics;
		ActorArchetypeDatabase				m_actorArchetypes;
		ActorLevelDatabase					m_actorLevels;

		eWorldRuntimeState					m_runtimeState	 = eWorldRuntimeState::Standby;
		Duration							m_tickIntervalNs = SIMULATION_TICK_NS;

	private:
		ShardDomain							m_tickDomain = {};
		bool								m_tickActive = false;
		bool								m_tickInProgress = false;
	};
}
