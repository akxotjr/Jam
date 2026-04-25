#pragma once

namespace jam
{
	class Mailbox;

	enum class eShardState : uint8
	{
		Closed		= 0,
		Open		= 1,
		Draining	= 2,
		Dead		= 3
	};

	struct QueueSlot
	{
		std::atomic<Mailbox*> q{ nullptr };                         // 현재 게시된 큐(없으면 nullptr)
		std::atomic<uint32>   gen{ 0 };                             // 세대 카운터(ABA/교체 감지)
		std::atomic<uint8>    state{ E2U(eShardState::Closed) };    // 운영 상태

		QueueSlot() = default;

		QueueSlot(const QueueSlot&) = delete;
		QueueSlot& operator=(const QueueSlot&) = delete;

		QueueSlot(QueueSlot&& other) noexcept
		{
			q.store(other.q.load(std::memory_order_relaxed), std::memory_order_relaxed);
			gen.store(other.gen.load(std::memory_order_relaxed), std::memory_order_relaxed);
			state.store(other.state.load(std::memory_order_relaxed), std::memory_order_relaxed);
		}

		QueueSlot& operator=(QueueSlot&& other) noexcept
		{
			if (this != &other)
			{
				q.store(other.q.load(std::memory_order_relaxed), std::memory_order_relaxed);
				gen.store(other.gen.load(std::memory_order_relaxed), std::memory_order_relaxed);
				state.store(other.state.load(std::memory_order_relaxed), std::memory_order_relaxed);
			}
			return *this;
		}
	};

	struct ShardSlot
	{
		QueueSlot inbox;	// 단일 ingress 슬롯 (채널 제거)
		uint32    shardId = 0;

		ShardSlot() = default;

		ShardSlot(const ShardSlot&) = delete;
		ShardSlot& operator=(const ShardSlot&) = delete;

		ShardSlot(ShardSlot&& other) noexcept : inbox(std::move(other.inbox)) , shardId(other.shardId) {}

		ShardSlot& operator=(ShardSlot&& other) noexcept
		{
			if (this != &other)
			{
				inbox = std::move(other.inbox);
				shardId = other.shardId;
			}
			return *this;
		}
	};
}
