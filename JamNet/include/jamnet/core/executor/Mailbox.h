#pragma once
#include "concurrentqueue/concurrentqueue.h"

#include "jamnet/core/executor/Job.h"


namespace jam
{
	using namespace moodycamel;

	class ShardExecutor;


	// Mailbox: 단일 소비자(ShardExecutor 스레드)만 Pop
	class Mailbox
	{
	public:
		explicit Mailbox(uint32 id, std::weak_ptr<ShardExecutor> owner);
		~Mailbox() = default;

		bool								Post(Job j);
		bool								Post(const ProducerToken& token, Job j);
		uint64								PostBulk(const ProducerToken& token, Job* j, uint64 count);


		bool								TryDequeue(OUT Job& j);
		uint64								TryDequeueBulk(OUT Job* j, uint64 count);
		uint64								TryDequeueBulk(OUT std::span<Job> jobs);

		template<typename OutputIt>
		uint64								TryDequeueBulk(OUT OutputIt out, uint64 count);

		bool								TryBeginConsume();
		void								EndConsume();

		bool								IsEmpty() const { return GetSizeApprox() == 0; }
		uint64								GetSizeApprox() const { return m_size.load(std::memory_order_relaxed); }
		uint32								GetId() const { return m_id; }
		bool								IsProcessing() const { return m_processing.load(std::memory_order_relaxed); }

	private:
		void								NotifyReadyIfFirst();

	private:
		uint32								m_id			= 0;
		std::weak_ptr<ShardExecutor>		m_owner;
		ConcurrentQueue<Job>				m_queue;				// MPSC
		ConsumerToken						m_consumerToken;
		std::atomic<uint64>					m_size			= 0;
		std::atomic<bool>					m_processing	= false;
	};



	template<typename OutputIt>
	uint64 Mailbox::TryDequeueBulk(OUT OutputIt out, uint64 count)
	{
		uint64 n = m_queue.try_dequeue_bulk(m_consumerToken, out, count);
		if (n > 0)
			m_size.fetch_sub(n, std::memory_order_relaxed);
		return n;
	}
}

