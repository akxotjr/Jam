#pragma once

#include "jamnet/runtime/actor/ActorArchetypeDatabase.h"
#include "jamnet/runtime/world/data/ActorLevelDatabase.h"
#include "jamnet/runtime/world/core/WorldBase.h"
#include "jamnet/sync/replication/ReplicationTypes.h"
#include "jamnet/sync/physics/ShardJobBridge.h"
#include "jamnet/sync/replication/NetActorComponents.h"

#include <jampx/IPhysicsFacade.h>
#include <jampx/PhysicsTypes.h>

#include <memory>
#include <utility>


namespace jam::net
{
	struct SpawnParams
	{
		// JamNet authoring/gameplay layer key.
		ActorArchetypeKey		actorArchetypeKey{};
		// JamPx execution layer key lives in desc.archetype.
		px::SpawnDesc			desc{};

		uint32					objectId = 0;
		uint32					spawnId  = 0;

		bool					owned = true;
		bool					controlled = false;

		uint64					owner = 0;
		uint64					controller = 0;

		px::ObjectId			targetObjectId = px::INVALID_OBJ_ID;
		NetId					targetNetId    = NetId::Invalid();
	};

	class PhysicalWorld : public WorldMembershipHost
	{
	public:
		PhysicalWorld() = default;
		PhysicalWorld(const WorldConfig& config);
		~PhysicalWorld() override = default;

		virtual void						Start(uint64 dt_ns) = 0;
		virtual void						Resume(uint64 dt_ns) = 0;
		virtual void						Stop() = 0;
		void								SetRuntimeState(ePhysicalWorldRuntimeState runtime);
		ePhysicalWorldRuntimeState			GetRuntimeState() const { return m_runtimeState; }
		void								SetTickIntervalNs(uint64 dt_ns) { m_tickIntervalNs = dt_ns; }
		void								SetActorArchetypeDatabase(ActorArchetypeDatabase database) { m_actorArchetypes = std::move(database); }
		void								SetActorLevelDatabase(ActorLevelDatabase database) { m_actorLevels = std::move(database); }

		entt::registry&						GetRegistry() { return m_registry; }

	protected:
		virtual void						Tick() = 0;

	protected:
		entt::registry						m_registry;
		std::unique_ptr<ShardJobBridge>		m_bridge;
		std::unique_ptr<px::IPhysicsFacade>	m_physics;
		ActorArchetypeDatabase				m_actorArchetypes;
		ActorLevelDatabase					m_actorLevels;

		ePhysicalWorldRuntimeState			m_runtimeState	 = ePhysicalWorldRuntimeState::Standby;
		uint64								m_tickIntervalNs = SIMULATION_TICK_NS;
	};
}
