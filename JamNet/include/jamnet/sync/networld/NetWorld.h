#pragma once

#include "jamnet/core/executor/Mailbox.h"
#include "jamnet/core/executor/ShardRoutingPolicy.h"
#include "jamnet/sync/physics/ShardJobBridge.h"
#include "jamnet/sync/replication/NetActorComponents.h"
#include "jamnet/runtime/world/WorldAssignmentTypes.h"

#include <jampx/PhysicsTypes.h>

#include <atomic>
#include <functional>



namespace jam::net
{

	struct SpawnParams
	{
		px::SpawnDesc			desc{};

		uint32					objectId = 0;
		uint32					spawnId  = 0;

		bool					owned = true;
		bool					controlled = false;
		
		uint64					owner = 0;		// owner userId
		uint64					controller = 0;	// controller userId

		px::ObjectId			targetObjectId = px::INVALID_OBJ_ID;		// for client-side
		NetId					targetNetId    = NetId::Invalid();			// for server-side
	};


	class NetWorld : public std::enable_shared_from_this<NetWorld>
	{
	public:
		NetWorld() = default;
		virtual ~NetWorld() = default;

		virtual void						Init();
		void								Tick(uint64 dt_ns);
		void								Stop();
		bool								BeginShutdown(eMailboxCloseMode mode = eMailboxCloseMode::Drain, std::function<void()> onClosed = {});

		void								Submit(Job j) const;
		bool								Post(Job j) const;

		entt::registry&						GetRegistry() { return m_world; }

		void								SetWorldId(WorldId worldId) { m_worldId = worldId; }
		WorldId								GetWorldId() const { return m_worldId; }

	private:
		void								FinalizeShutdownOnShard(std::function<void()> onClosed);
		virtual void						TickOnShard() = 0;

	protected:
		entt::registry						m_world;
		std::unique_ptr<ShardJobBridge>		m_bridge;

	private:
		RouteAssignment						m_route;
		std::weak_ptr<ShardExecutor>		m_boundShard;
		std::shared_ptr<Mailbox>			m_mailbox;

		bool								m_tickActive = false;
		std::atomic<bool>					m_shutdownRequested = false;

		WorldId								m_worldId = INVALID_WORLD_ID;
	};

}
