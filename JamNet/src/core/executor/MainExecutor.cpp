#include "pch.h"
#include "jamnet/core/executor/MainExecutor.h"
#include "jamnet/core/executor/ThreadContext.h"


namespace jam
{
	void MainExecutor::Init()
	{
		std::scoped_lock lock(m_lifecycleMutex);
		m_accepting.store(false, std::memory_order_release);
		DiscardPending();
		m_id = std::this_thread::get_id();
		InitThreadContext("MainExecutor", this);
		m_accepting.store(true, std::memory_order_release);
	}

	void MainExecutor::Shutdown()
	{
		std::scoped_lock lock(m_lifecycleMutex);
		m_accepting.store(false, std::memory_order_release);
		DiscardPending();
		m_id = {};
	}

	void MainExecutor::Submit(Job j)
	{
		std::scoped_lock lock(m_lifecycleMutex);
		if (!m_accepting.load(std::memory_order_acquire))
			return;

		m_queue.enqueue(std::move(j));
		m_pending.fetch_add(1, std::memory_order_release);
	}

	size_t MainExecutor::PumpOnce(size_t maxCnt)
	{
		assert(IsMainThread() && "PumpOnce must be called on main thread");
		size_t n = 0;
		for (; n < maxCnt; ++n)
		{
			if (m_pending.load(std::memory_order_acquire) == 0)
				break;

			Job j;
			if (!m_queue.try_dequeue(j))
				break;

			m_pending.fetch_sub(1, std::memory_order_release);
			j.Execute();
		}
		return n;
	}

	void MainExecutor::DrainAll()
	{
		assert(IsMainThread() && "DrainAll must be called on main thread");
		Job j;
		while (m_queue.try_dequeue(j))
		{
			m_pending.fetch_sub(1, std::memory_order_release);
			j.Execute();
		}
	}

	void MainExecutor::DiscardPending()
	{
		Job discarded;
		while (m_queue.try_dequeue(discarded))
		{
		}
		m_pending.store(0, std::memory_order_release);
	}
}
