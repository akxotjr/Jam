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
		if (!IsAcceptingPosts())
			return false;

		bool expected = m_queue.enqueue(std::move(j));
		if (expected)
		{
			m_size.fetch_add(1, std::memory_order_relaxed);
			NotifyReadyIfFirst();
		}
		return expected;
	}

	bool Mailbox::Post(const ProducerToken& token, Job j)
	{
		if (!IsAcceptingPosts())
			return false;

		bool expected = m_queue.enqueue(token, std::move(j));
		if (expected)
		{
			m_size.fetch_add(1, std::memory_order_relaxed);
			NotifyReadyIfFirst();
		}
		return expected;
	}

	uint64 Mailbox::PostBulk(const ProducerToken& token, Job* j, uint64 count)
	{
		if (!IsAcceptingPosts())
			return 0;

		const bool enqueued = m_queue.try_enqueue_bulk(token, j, count);
		if (enqueued)
		{
			m_size.fetch_add(count, std::memory_order_relaxed);
			NotifyReadyIfFirst();
		}
		return enqueued ? count : 0;
	}

	bool Mailbox::Close(eMailboxCloseMode mode, std::function<void()> onClosed)
	{
		EnqueueCloseCallback(std::move(onClosed));

		eMailboxState desired = (mode == eMailboxCloseMode::Abort)
			? eMailboxState::ClosingAbort
			: eMailboxState::ClosingDrain;

		eMailboxState state = m_state.load(std::memory_order_acquire);
		for (;;)
		{
			if (state == eMailboxState::Closed)
			{
				InvokeCloseCallbacks();
				return false;
			}

			if (state == eMailboxState::ClosingAbort)
				break;

			if (state == desired)
				break;

			if (state == eMailboxState::ClosingDrain && desired == eMailboxState::ClosingAbort)
			{
				if (m_state.compare_exchange_weak(state, desired, std::memory_order_acq_rel, std::memory_order_acquire))
					break;
				continue;
			}

			if (state == eMailboxState::Open)
			{
				if (m_state.compare_exchange_weak(state, desired, std::memory_order_acq_rel, std::memory_order_acquire))
					break;
				continue;
			}

			break;
		}

		NotifyReadyIfFirst();
		return true;
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
		// A failed claim still performs a release RMW. The current consumer's
		// EndConsume() acquires it, so posts made while owned cannot lose a wakeup.
		return !m_processing.exchange(true, std::memory_order_acq_rel);
	}

	void Mailbox::EndConsume()
	{
		// Acquire producer RMWs before releasing ownership and checking m_size.
		m_processing.exchange(false, std::memory_order_acq_rel);
	}

	void Mailbox::OnDequeuedForExecution(uint64 count)
	{
		if (count == 0)
			return;

		m_inFlight.fetch_add(count, std::memory_order_relaxed);
	}
	
	void Mailbox::OnJobExecuted()
	{
		const uint64 prev = m_inFlight.fetch_sub(1, std::memory_order_relaxed);
		if (prev != 1)
			return;

		const eMailboxState state = m_state.load(std::memory_order_acquire);
		if (state == eMailboxState::Open || state == eMailboxState::Closed)
			return;

		if (!IsEmpty())
			return;

		if (!TryBeginConsume())
			return;

		if (auto owner = m_owner.lock())
		{
			if (!owner->NotifyReady(m_id))
				EndConsume();
		}
		else
			EndConsume();
	}

	uint64 Mailbox::DiscardPending()
	{
		uint64 discarded = 0;
		Job ignored;
		while (TryDequeue(ignored))
			++discarded;
		return discarded;
	}

	bool Mailbox::TryFinalizeClose()
	{
		const eMailboxState state = m_state.load(std::memory_order_acquire);
		if (state == eMailboxState::Open || state == eMailboxState::Closed)
			return state == eMailboxState::Closed;

		if (IsProcessing())
			return false;

		if (state == eMailboxState::ClosingDrain && (!IsEmpty() || m_inFlight.load(std::memory_order_acquire) != 0))
			return false;

		if (state == eMailboxState::ClosingAbort && m_inFlight.load(std::memory_order_acquire) != 0)
			return false;

		eMailboxState expected = state;
		if (!m_state.compare_exchange_strong(expected, eMailboxState::Closed, std::memory_order_acq_rel, std::memory_order_acquire))
			return expected == eMailboxState::Closed;

		InvokeCloseCallbacks();
		return true;
	}

	bool Mailbox::IsClosing() const
	{
		const eMailboxState state = m_state.load(std::memory_order_acquire);
		return state == eMailboxState::ClosingDrain || state == eMailboxState::ClosingAbort;
	}

	bool Mailbox::NotifyReadyIfFirst()
	{
		if (!TryBeginConsume())
			return false;

		if (auto owner = m_owner.lock())
		{
			if (owner->NotifyReady(m_id))
				return true;

			EndConsume();
			return false;
		}
		else
		{
			EndConsume();
			return false;
		}
	}

	void Mailbox::EnqueueCloseCallback(std::function<void()> onClosed)
	{
		if (!onClosed)
			return;

		std::scoped_lock guard(m_closeMutex);
		m_closeCallbacks.push_back(std::move(onClosed));
	}

	void Mailbox::InvokeCloseCallbacks()
	{
		std::vector<std::function<void()>> callbacks;
		{
			std::scoped_lock guard(m_closeMutex);
			callbacks.swap(m_closeCallbacks);
		}

		for (auto& callback : callbacks)
		{
			if (callback)
				callback();
		}
	}
}
 
