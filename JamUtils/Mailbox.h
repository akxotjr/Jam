#pragma once
#include "concurrentqueue/concurrentqueue.h"
#include "Job.h"
#include "ShardSlot.h"

namespace jam::utils::exec
{

	class ShardExecutor;



	// Mailbox: 단일 소비자(ShardExecutor 스레드)만 Pop
	class Mailbox
	{
	public:
		explicit Mailbox(uint32 id, Wptr<ShardExecutor> owner, eMailboxChannel channel = eMailboxChannel::NORMAL);
		~Mailbox() = default;

		bool			Post(job::Job job);
		bool			Post(const moodycamel::ProducerToken& token, job::Job job);
		uint64			PostBulk(const moodycamel::ProducerToken& token, job::Job* job, uint64 count);


		bool			TryPop(OUT job::Job& job);
		uint64			TryPopBulk(OUT job::Job* job, uint64 count);

		template<typename OutputIt>
		uint64			TryPopBulk(OUT OutputIt out, uint64 count);


		bool			TryBeginConsume();
		void			EndConsume();

		bool			IsEmpty() const { return GetSizeApprox() == 0; }
		uint64			GetSizeApprox() const { return m_size.load(std::memory_order_relaxed); }
		uint32			GetId() const { return m_id; }
		bool			IsProcessing() const { return m_processing.load(std::memory_order_relaxed); }


		eMailboxChannel GetMailboxChannel() const { return m_channel; }

	private:
		void			NotifyReadyIfFirst();

	private:
		uint32										m_id = 0;
		Wptr<ShardExecutor>							m_owner;
		moodycamel::ConcurrentQueue<job::Job>		m_queue; // MPSC
		moodycamel::ConsumerToken					m_consumerToken;
		Atomic<uint64>								m_size{ 0 };
		Atomic<bool>								m_processing{ false };

		eMailboxChannel								m_channel;
	};



	template<typename OutputIt>
	inline uint64 Mailbox::TryPopBulk(OUT OutputIt out, uint64 count)
	{
		uint64 n = m_queue.try_dequeue_bulk(m_consumerToken, out, count);
		if (n > 0)
			m_size.fetch_sub(n, std::memory_order_relaxed);
		return n;
	}
}

