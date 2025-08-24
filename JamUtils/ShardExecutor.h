#pragma once
#include "concurrentqueue/concurrentqueue.h"
#include "Mailbox.h"
#include "FiberScheduler.h"
#include "Job.h"
#include "NumaTopology.h"

namespace jam::utils::exec
{
	class GlobalExecutor; // fwd

	struct ShardExecutorConfig
	{
		int32		index = 0;
		int32		batchBudget = 32;    // Mailbox�� 1ȸ ó����
		int32		idleSleepMs = 1;     // ���� ����
		uint64		assistThreshold = 512; // Mailbox ���� �Ӱ�ġ

		uint16		numaNode = 0xFFFF;	// opt
	};

	class ShardExecutor : public std::enable_shared_from_this<ShardExecutor>
	{
	public:
		explicit ShardExecutor(const ShardExecutorConfig& config = {}, std::weak_ptr<GlobalExecutor> owner = {});
		~ShardExecutor();

		void                        Start();
		void                        Stop();
		void                        Join();

		// ���� ���ο� ���� (�ɼ�: ���� ���� �۾�)
		void                        Submit(job::Job job);

		// Mailbox ����
		std::shared_ptr<Mailbox>    CreateMailbox();
		void                        RemoveMailbox(uint32 id);

		// Global�� ȣ���ϴ� ���� Drain
		void                        AssistDrainOnce(int32 maxMailboxes, int32 budgetPerMailbox);

		// Mailbox�� 0��1 ���� �� ȣ��
		void                        NotifyReady(Mailbox* mb);

		int32                       GetIndex() const { return m_config.index; }


		// NUMA / Shard Pinning
		void						SetPinSlot(const utils::sys::CoreSlot& slot);
		uint16						GetNumaNode() const { return m_config.numaNode; }



	private:
		void                        Loop();
		bool                        ProcessReadyOnce();
		void                        ProcessMailbox(Mailbox* mb, int32 budget);
		void                        RequestAssistIfNeeded(Mailbox* mb);

	private:
		ShardExecutorConfig                                 m_config{};
		std::weak_ptr<GlobalExecutor>                       m_owner;         // Assist ��û ����

		Atomic<bool>                                        m_running{ false };
		std::thread                                         m_thread;

		// per-shard FiberScheduler (ť�� ����)
		Uptr<thrd::FiberScheduler>                          m_scheduler;
		void*                                               m_mainFiber = nullptr;

		// shard ť (MPSC ����)
		moodycamel::ConcurrentQueue<job::Job>               m_queue;

		// ready Mailbox ��� (MPSC, Mailbox�� 0��1 ���� �� push)
		moodycamel::ConcurrentQueue<Mailbox*>               m_ready;

		// Mailbox ���� (����)
		USE_LOCK
		xmap<uint32, std::shared_ptr<Mailbox>>              m_mailboxes;
		Atomic<uint32>                                      m_nextMailboxId{ 1 };

		// Assist ���� ��û ����
		Atomic<bool>                                        m_assistRequested{ false };

		// Shard Pinning
		bool m_pinEnabled = false;
		utils::sys::CoreSlot m_pinSlot = {};
	};
}
