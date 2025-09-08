#pragma once
#include "concurrentqueue/concurrentqueue.h"
#include "Mailbox.h"
#include "Job.h"
#include "NumaTopology.h"
#include "ShardSlot.h"
#include "ConcurrentQueueToken.h"


namespace jam::utils::exec
{
	class GlobalExecutor; // fwd


	struct ShardLocal
	{
		entt::registry		world;
		entt::dispatcher	events;	// for optimization hot-pass

		// std::unordered_map<GroupId, std::vector<entt::entity>> groupIndex;

		std::vector<std::function<void(entt::registry&)>> defers; //���� �ݿ��� : ������ ���� ������ �۾�

		using SystemFn = void(*)(ShardLocal&, uint64 now_ns, uint64 dt_ns);	//system runner
		std::vector<SystemFn> systems;
	};



	struct ShardExecutorConfig
	{
		int32		index = 0;
		int32		batchBudget = 32;    // Mailbox�� 1ȸ ó����
		int32		idleSleepMs = 1;     // ���� ����
		uint64		assistThreshold = 512; // Mailbox ���� �Ӱ�ġ

		uint16		numaNode = 0xFFFF;	// opt
	};


	struct GroupLocal
	{
		// �� ���忡 �����á��� �پ��ִ� ���ǵ��� Mailbox (Normal ��� ���)
		std::vector<std::weak_ptr<Mailbox>> members;
	};

	struct GroupHome
	{
		// � ���忡 ����� �ִ���: ���� �ε����� refcount (0�̸� ����)
		std::vector<uint32> shard_refcnt;
		// (�ɼ�) ������/��������/�׷� ť
		// std::shared_ptr<Mailbox> qNorm, qCtrl;
		uint64 seq = 0;
	};


	class ShardExecutor : public std::enable_shared_from_this<ShardExecutor>
	{
	public:
		explicit ShardExecutor(const ShardExecutorConfig& config = {}, std::weak_ptr<GlobalExecutor> owner = {});
		~ShardExecutor();

		void                        Start();
		void                        Stop();
		void                        Join();

		void						AttachSlot(ShardSlot* slot) { m_shardSlot = slot; }

		// ���� ���ο� ���� (�ɼ�: ���� ���� �۾�)
		void                        Submit(job::Job job);

		// Mailbox ����
		std::shared_ptr<Mailbox>    CreateMailbox(eMailboxChannel channel = eMailboxChannel::NORMAL);
		void                        RemoveMailbox(uint32 id);

		void BeginDrain();
		// Global�� ȣ���ϴ� ���� Drain
		void                        AssistDrainOnce(int32 maxMailboxes, int32 budgetPerMailbox);

		// Mailbox�� 0��1 ���� �� ȣ��
		void                        NotifyReady(Mailbox* mb);

		int32                       GetIndex() const { return m_config.index; }


		// NUMA / Shard Pinning
		void						PinCoreSlot(const utils::sys::CoreSlot& slot);
		uint16						GetNumaNode() const { return m_config.numaNode; }

		// Fiber
		void						SpawnFiber(thrd::FiberFn fn, const thrd::FiberDesc& desc);
		void						ResumeFiber(thrd::AwaitKey key);
		void						CancelFiberByKey(thrd::AwaitKey key, thrd::eCancelCode code);
		void						CancelFiberById(uint32 id, thrd::eCancelCode code);


		// Routing
		// �Ʒ� �ڵ鷯���� ������ �����忡���� ����Ǵ� Job ���� ȣ��
		void OnGroupLocalJoin(uint64 group_id, std::shared_ptr<Mailbox> mailbox);
		void OnGroupLocalLeave(uint64 group_id, std::shared_ptr<Mailbox> mailbox);
		void OnGroupHomeMark(uint64 group_id, uint32 shardIdx, int32 delta); // +1 or -1

		void OnGroupMulticastHome(uint64 group_id, job::Job j);     // Ȩ ���忡�� ����
		void OnGroupMulticastRemote(uint64 group_id, job::Job j);   // ���� ���忡�� ����

		// ��ƿ: ���� ������� ���
		void DeliverToLocal(uint64 group_id, job::Job j);



		ShardLocal& Local() { return m_local; }
		const ShardLocal& Local() const { return m_local; }
		void Tick(uint64 now_ns, uint64 dt_ns);


	private:
		void                        Loop();
		bool                        ProcessReadyOnce();
		void                        ProcessMailbox(Mailbox* mb, int32 budget);
		void                        RequestAssistIfNeeded(Mailbox* mb);

		bool						TryDequeueReady(OUT Mailbox*& mailbox);

	private:
		ShardExecutorConfig                                 m_config{};
		std::weak_ptr<GlobalExecutor>                       m_owner;         // Assist ��û ����

		Atomic<bool>                                        m_running{ false };
		std::thread                                         m_thread;

		// per-shard FiberScheduler (ť�� ����)
		thrd::WinFiberBackend								m_backend;
		Uptr<thrd::FiberScheduler>                          m_scheduler;
		void*                                               m_mainFiber = nullptr;


		ShardSlot*											m_shardSlot = nullptr;

		// shard ť (MPSC ����)
		moodycamel::ConcurrentQueue<job::Job>               m_shardsQ;
		Uptr<moodycamel::ConsumerToken>						m_shardsCtok;
		// ready Mailbox ��� (MPSC, Mailbox�� 0��1 ���� �� push)
		moodycamel::ConcurrentQueue<Mailbox*>               m_readyCtrlQ;
		moodycamel::ConcurrentQueue<Mailbox*>				m_readyNormalQ;
		Uptr<moodycamel::ConsumerToken>						m_readyCtrlCtok;
		Uptr<moodycamel::ConsumerToken>						m_readyNormalCtok;

		// Mailbox ���� (����)
		USE_LOCK
		xmap<uint32, std::shared_ptr<Mailbox>>              m_mailboxes;
		Atomic<uint32>                                      m_nextMailboxId{ 1 };

		// Assist ���� ��û ����
		Atomic<bool>                                        m_assistRequested{ false };

		// Shard Pinning
		bool												m_pinEnabled = false;
		utils::sys::CoreSlot								m_pinSlot = {};


		// ����-���� �׷� ����(�� ������ ���� ��� ���)
		std::unordered_map<uint64, GroupLocal>				m_groupLocal;
		// Ȩ ����(���� Ȩ�� �׷���� ����/��Ÿ)
		std::unordered_map<uint64, GroupHome>				m_groupHome;

		ShardLocal											m_local;
	};
}
