#include "pch.h"
#include "jamnet/core/executor/Mailbox.h"
#include "jamnet/core/executor/ShardExecutor.h"

namespace jam
{
	Mailbox::Mailbox(uint32 id, std::weak_ptr<ShardExecutor> owner)
		: m_id(id), m_owner(std::move(owner)), m_consumerToken(m_queue)
	{
	}

	bool Mailbox::Post(Job j)
	{
		bool expected = m_queue.enqueue(std::move(j));
		if (expected)
		{
			const uint64 prev = m_size.fetch_add(1, std::memory_order_relaxed);
			if (prev == 0)
				NotifyReadyIfFirst();
		}
		return expected;
	}

	bool Mailbox::Post(const ProducerToken& token, Job j)
	{
		bool expected = m_queue.enqueue(token, std::move(j));
		if (expected)
		{
			const uint64 prev = m_size.fetch_add(1, std::memory_order_relaxed);
			if (prev == 0)
				NotifyReadyIfFirst();
		}
		return expected;
	}

	uint64 Mailbox::PostBulk(const ProducerToken& token, Job* j, uint64 count)
	{
		bool expected = m_queue.try_enqueue_bulk(token, j, count);
		if (expected)
		{
			const uint64 prev = m_size.fetch_add(count, std::memory_order_relaxed);
			if (prev == 0)
				NotifyReadyIfFirst();
		}
		return expected;
	}

	bool Mailbox::TryDequeue(OUT Job& j)
	{
		if (m_queue.try_dequeue(m_consumerToken, j))
		{
			m_size.fetch_sub(1, std::memory_order_relaxed);
			return true;
		}
		return false;
	}

	uint64 Mailbox::TryDequeueBulk(OUT Job* j, uint64 count)
	{
		uint64 n = m_queue.try_dequeue_bulk(m_consumerToken, j, count);
		if (n > 0)
			m_size.fetch_sub(n, std::memory_order_relaxed);
		return n;
	}

	uint64 Mailbox::TryDequeueBulk(OUT std::span<Job> jobs)
	{
		if (jobs.empty()) return 0;

		uint64 n = m_queue.try_dequeue_bulk(m_consumerToken, jobs.data(), jobs.size());
		if (n > 0)
			m_size.fetch_sub(n, std::memory_order_relaxed);

		return n;
	}

	bool Mailbox::TryBeginConsume()
	{
		bool expected = false;
		return m_processing.compare_exchange_strong(expected, true, std::memory_order_relaxed);
	}

	void Mailbox::EndConsume()
	{
		m_processing.store(false, std::memory_order_relaxed);
	}

	void Mailbox::NotifyReadyIfFirst()
	{
		if (!TryBeginConsume())
			return;

		if (auto owner = m_owner.lock())
			owner->NotifyReady(this);
		else
			EndConsume();
	}
}
 