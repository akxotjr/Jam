#pragma once

#include <jampx/PhysicsTypes.h>

#include "jamnet/sync/physics/ShardJobBridge.h"

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
	};


	class NetWorld : public std::enable_shared_from_this<NetWorld>
	{
	public:
		NetWorld() = default;
		virtual ~NetWorld() = default;

		virtual void					Init();
		void							Tick(uint64 dt_ns);
		void							Stop();

		void							Submit(Job j) const;
		void							Post(Job j) const;

		entt::registry&					GetRegistry() { return m_world; }

		void							SetGroupId(uint32 groupId) { m_groupId = groupId; }
		uint32							GetGroupId() const { return m_groupId; }

	private:
		virtual void					TickOnShard() = 0;

	protected:
		entt::registry					m_world;
		unique_ptr<ShardJobBridge>		m_bridge;

	private:
		RouteKey						m_key;
		weak_ptr<ShardExecutor>			m_boundShard;
		shared_ptr<Mailbox>				m_mailbox;

		bool							m_tickActive = false;

		uint32							m_groupId = 0;
	};

}
