#pragma once
#include "concurrentqueue/concurrentqueue.h"

namespace jam::utils::exec
{
	class Mailbox
	{
	public:
		Mailbox();
		~Mailbox();

		bool TryPush(job::Job job);
		bool TryPop(OUT job::Job& job);

		bool IsEmpty() const { return GetSizeApprox() == 0; }
		uint64 GetSizeApprox() const { return m_size.load(std::memory_order_relaxed); }

	private:
		moodycamel::ConcurrentQueue<job::Job>	m_queue;
		Atomic<uint64>							m_size;
	};
}

