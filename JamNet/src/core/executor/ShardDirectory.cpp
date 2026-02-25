#include "pch.h"
#include "jamnet/core/executor/ShardDirectory.h"

namespace jam
{
	ShardDirectory::~ShardDirectory()
	{
		try { StopAll(); JoinAll(); } catch (...) {}
	}

	void ShardDirectory::Init()
	{
		m_shards.reserve(m_config.numShards);
		for (uint32 i = 0; i < m_config.numShards; ++i)
		{
			ShardExecutorConfig c = m_config.shardCfg;
			c.index = static_cast<int32>(i);
			auto shard = std::make_shared<ShardExecutor>(c);
			m_shards.emplace_back(std::move(shard));
		}
	}

	void ShardDirectory::Start()
	{
		for (auto& s : m_shards)
			if (s) s->Start();

		AttachSlots();
	}

	void ShardDirectory::StopAll() const
	{
		for (auto& s : m_shards)
			if (s) s->BeginDrain();
		for (auto& s : m_shards)
			if (s) s->Stop();
	}

	void ShardDirectory::JoinAll() const
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
		return Mix64(key) % n;		// return shard index
	}

	shared_ptr<ShardExecutor> ShardDirectory::ShardAt(uint64 index) const
	{
		if (index >= Size()) return {};
		return m_shards[index];
	}
}
