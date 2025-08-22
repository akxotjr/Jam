#include "pch.h"
#include "Mailbox.h"


namespace jam::utils::exec
{
	bool Mailbox::TryPush(job::Job job)
	{
		bool ok = m_queue.enqueue(std::move(job));
		if (ok)
			m_size.fetch_add(1, std::memory_order_relaxed);
		return ok;
	}

	bool Mailbox::TryPop(OUT job::Job& job)
	{
		if (m_queue.try_dequeue(job)) 
		{
			m_size.fetch_sub(1, std::memory_order_relaxed);
			return true;
		}
		return false;
	}

}
