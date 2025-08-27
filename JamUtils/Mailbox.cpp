#include "pch.h"
#include "Mailbox.h"
#include "ShardExecutor.h"

namespace jam::utils::exec
{
	Mailbox::Mailbox(uint32 id, Wptr<ShardExecutor> owner, eMailboxChannel channel)
		: m_id(id), m_owner(std::move(owner)), m_consumerToken(m_queue), m_channel(channel)
	{
	}

	bool Mailbox::Post(job::Job job)
	{
		bool expected = m_queue.enqueue(std::move(job));
		if (expected)
		{
			const uint64 prev = m_size.fetch_add(1, std::memory_order_relaxed);
			if (prev == 0)
				NotifyReadyIfFirst();
		}
		return expected;
	}

	bool Mailbox::Post(const moodycamel::ProducerToken& token, job::Job job)
	{
		bool expected = m_queue.enqueue(token, std::move(job));
		if (expected)
		{
			const uint64 prev = m_size.fetch_add(1, std::memory_order_relaxed);
			if (prev == 0)
				NotifyReadyIfFirst();
		}
		return expected;
	}

	uint64 Mailbox::PostBulk(const moodycamel::ProducerToken& token, job::Job* job, uint64 count)
	{
		bool expected = m_queue.try_enqueue_bulk(token, job, count);
		if (expected)
		{
			const uint64 prev = m_size.fetch_add(count, std::memory_order_relaxed);
			if (prev == 0)
				NotifyReadyIfFirst();
		}
		return expected;
	}

	bool Mailbox::TryPop(OUT job::Job& job)
	{
		if (m_queue.try_dequeue(m_consumerToken, job))
		{
			m_size.fetch_sub(1, std::memory_order_relaxed);
			return true;
		}
		return false;
	}

	uint64 Mailbox::TryPopBulk(job::Job* job, uint64 count)
	{
		uint64 n = m_queue.try_dequeue_bulk(m_consumerToken, job, count);
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
		if (auto owner = m_owner.lock())
			owner->NotifyReady(this);
	}
}
 