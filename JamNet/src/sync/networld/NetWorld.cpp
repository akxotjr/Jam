#include "pch.h"
#include "jamnet/sync/networld/NetWorld.h"


namespace jam::net
{
	void NetWorld::Init()
	{
		m_key = GLOBAL_EXEC.MakeRouteKey("NetWorld", reinterpret_cast<uint64>(this));
		auto shard = GLOBAL_EXEC.GetShard(m_key);
		if (!shard) return;

		m_boundShard = shard;
		m_mailbox	 = shard->CreateMailbox();
		m_bridge     = std::make_unique<ShardJobBridge>(*shard);
	}

	void NetWorld::Tick(uint64 dt_ns)
	{
		auto shard = m_boundShard.lock();
		if (!shard) return;

		m_tickActive = true;

		auto self = shared_from_this();
		const WorldId worldId = m_worldId; // 캡처 시점에 고정

		shard->Submit(Job([this, shard, dt_ns, self, worldId]
			{
				auto& L		= shard->Local();
				auto& group = L.domainGroups[{ DOMAIN_NETWORK, worldId }]; // subType = worldId

				group.tickPeriod_ns = dt_ns;
				group.systems.emplace_back([self, this](ShardLocal&, uint64, uint64)
					{
						if (!m_tickActive) return;
						self->TickOnShard();
					});
			}));
	}

	void NetWorld::Stop()
	{
		m_tickActive = false;
	}

	void NetWorld::Submit(Job j) const
	{
		if (auto shard = m_boundShard.lock())
			shard->Submit(std::move(j));
	}

	void NetWorld::Post(Job j) const
	{
		if (m_mailbox) m_mailbox->Post(std::move(j));
	}
}
