#pragma once
#include "jamnet/core/executor/Job.h"

#include "concurrentqueue/moodycamel/concurrentqueue.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <span>
#include <vector>


namespace jam
{
	using namespace moodycamel;

	class ShardExecutor;

	enum class eMailboxCloseMode : uint8
	{
		Drain			= 0,
		Abort			= 1,
	};

	enum class eMailboxState : uint8
	{
		Open			= 0,
		ClosingDrain	= 1,
		ClosingAbort	= 2,
		Closed			= 3,
	};


	// Mailbox: 단일 소비자(ShardExecutor 스레드)만 Pop
	class Mailbox
	{
	public:
		explicit Mailbox(uint32 id, std::weak_ptr<ShardExecutor> owner);
		~Mailbox() = default;

		bool								Post(Job j);
		bool								Post(const ProducerToken& token, Job j);
		uint64								PostBulk(const ProducerToken& token, Job* j, uint64 count);
		bool								Close(eMailboxCloseMode mode, std::function<void()> onClosed = {});


		bool								TryDequeue(OUT Job& j);
		uint64								TryDequeueBulk(OUT Job* j, uint64 count);
		uint64								TryDequeueBulk(OUT std::span<Job> jobs);

		template<typename OutputIt>
		uint64								TryDequeueBulk(OUT OutputIt out, uint64 count);

		bool								TryBeginConsume();
		void								EndConsume();
		void								OnDequeuedForExecution(uint64 count);
		void								OnJobExecuted();
		uint64								DiscardPending();
		bool								TryFinalizeClose();
		bool								ConsumeRepostRequested();
		bool								RequestRepost();

		bool								IsEmpty()			 const { return GetSizeApprox() == 0; }
		uint64								GetSizeApprox()		 const { return m_size.load(std::memory_order_relaxed); }
		uint32								GetId()				 const { return m_id; }
		bool								IsProcessing()		 const { return m_processing.load(std::memory_order_relaxed); }
		bool								IsAcceptingPosts()   const { return m_state.load(std::memory_order_acquire) == eMailboxState::Open; }
		bool								IsClosing()			 const;
		bool								IsClosed()			 const { return m_state.load(std::memory_order_acquire) == eMailboxState::Closed; }
		bool								ShouldAbortPending() const { return m_state.load(std::memory_order_acquire) == eMailboxState::ClosingAbort; }
		eMailboxState						GetState()			 const { return m_state.load(std::memory_order_acquire); }

	private:
		bool								NotifyReadyIfFirst();
		void								EnqueueCloseCallback(std::function<void()> onClosed);
		void								InvokeCloseCallbacks();

	private:
		uint32								m_id			= 0;
		std::weak_ptr<ShardExecutor>		m_owner;
		ConcurrentQueue<Job>				m_queue;			
		ConsumerToken						m_consumerToken;
		std::atomic<uint64>					m_size			= 0;
		std::atomic<uint64>					m_inFlight		= 0;
		std::atomic<bool>					m_processing	= false;
		std::atomic<bool>					m_repostRequested = false;
		std::atomic<eMailboxState>			m_state			= eMailboxState::Open;

		std::mutex							m_closeMutex;
		std::vector<std::function<void()>>	m_closeCallbacks;
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
