#include "pch.h"
#include "ShardDirectory.h"

namespace jam::utils::exec
{
	static inline uint64 Mix64(uint64 x)
	{
		x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
		x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL;
		x ^= x >> 33;
		return x;
	}



	ShardDirectory::ShardDirectory(const ShardDirectoryConfig& cfg, std::weak_ptr<GlobalExecutor> owner)
		: m_config(cfg), m_owner(std::move(owner))
	{
	}

	ShardDirectory::~ShardDirectory()
	{
		try { StopAll(); JoinAll(); } catch (...) {}
	}

	void ShardDirectory::Init(const std::vector<Sptr<ShardExecutor>>& shards)
	{
		if (m_config.ownership == eShardOwnership::OWN)
		{
			m_shards.reserve(m_config.numShards);
			for (uint32 i = 0; i < m_config.numShards; ++i)
			{
				ShardExecutorConfig c = m_config.shardCfg;
				c.index = static_cast<int32>(i);

				auto shard = std::make_shared<ShardExecutor>(c, m_owner);
				m_shards.emplace_back(std::move(shard));
			}
		}
		else if (m_config.ownership == eShardOwnership::ADOPT)
		{
			m_shards = shards;
		}
	}

	void ShardDirectory::Start()
	{
		for (auto& s : m_shards)
			if (s) s->Start();


		AttachSlots();
	}

	void ShardDirectory::StopAll()
	{
		for (auto& s : m_shards)
			if (s) s->BeginDrain();
		for (auto& s : m_shards)
			if (s) s->Stop();
	}

	void ShardDirectory::JoinAll()
	{
		for (auto& s : m_shards)
			if (s) s->Join();
	}

	void ShardDirectory::AttachSlots()
	{
		const uint64 n = static_cast<uint64>(m_shards.size());
		m_slots.resize(n);

		for (uint64 i = 0; i < n; ++i)
		{
			m_slots[i].shardId = static_cast<uint32>(i);
			if (m_shards[i])
				m_shards[i]->AttachSlot(&m_slots[i]);
		}
	}

	uint64 ShardDirectory::Size() const
	{
		return static_cast<uint64>(m_shards.size());
	}

	uint64 ShardDirectory::PickShard(uint64 key) const
	{
		const uint64 n = Size();
		if (n == 0) return 0;
		return Mix64(key) % n;
	}

	Sptr<ShardExecutor> ShardDirectory::ShardAt(uint64 i) const
	{
		if (i >= Size()) return {};
		return m_shards[i];
	}

	ShardEndpoint ShardDirectory::EndpointFor(uint64 key) const
	{
		const uint64 idx = PickShard(key);
		auto s = ShardAt(idx);
		return {std::move(s)};
	}

	ShardEndpoint ShardDirectory::EndpointFor(uint64 key, eMailboxChannel channel) const
	{
		const uint64 n = Size();
		if (n == 0)
			return { Sptr<ShardExecutor>{} };

		const uint64 idx = PickShard(key);

		if (idx < static_cast<uint64>(m_slots.size()))
			return { const_cast<ShardSlot*>(&m_slots[idx]), channel };

		auto s = ShardAt(idx);
		return {std::move(s)};
	}
}
