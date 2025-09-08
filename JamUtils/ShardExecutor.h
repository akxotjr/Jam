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

		std::vector<std::function<void(entt::registry&)>> defers; //지연 반영용 : 프레임 끝에 실행할 작업

		using SystemFn = void(*)(ShardLocal&, uint64 now_ns, uint64 dt_ns);	//system runner
		std::vector<SystemFn> systems;
	};



	struct ShardExecutorConfig
	{
		int32		index = 0;
		int32		batchBudget = 32;    // Mailbox당 1회 처리량
		int32		idleSleepMs = 1;     // 유휴 슬립
		uint64		assistThreshold = 512; // Mailbox 길이 임계치

		uint16		numaNode = 0xFFFF;	// opt
	};


	struct GroupLocal
	{
		// 이 샤드에 “로컬”로 붙어있는 세션들의 Mailbox (Normal 배송 대상)
		std::vector<std::weak_ptr<Mailbox>> members;
	};

	struct GroupHome
	{
		// 어떤 샤드에 멤버가 있는지: 샤드 인덱스별 refcount (0이면 없음)
		std::vector<uint32> shard_refcnt;
		// (옵션) 시퀀싱/백프레셔/그룹 큐
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

		// 샤드 내부용 주입 (옵션: 샤드 레벨 작업)
		void                        Submit(job::Job job);

		// Mailbox 관리
		std::shared_ptr<Mailbox>    CreateMailbox(eMailboxChannel channel = eMailboxChannel::NORMAL);
		void                        RemoveMailbox(uint32 id);

		void BeginDrain();
		// Global이 호출하는 보조 Drain
		void                        AssistDrainOnce(int32 maxMailboxes, int32 budgetPerMailbox);

		// Mailbox가 0→1 전이 시 호출
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
		// 아래 핸들러들은 “샤드 스레드에서” 실행되는 Job 으로 호출
		void OnGroupLocalJoin(uint64 group_id, std::shared_ptr<Mailbox> mailbox);
		void OnGroupLocalLeave(uint64 group_id, std::shared_ptr<Mailbox> mailbox);
		void OnGroupHomeMark(uint64 group_id, uint32 shardIdx, int32 delta); // +1 or -1

		void OnGroupMulticastHome(uint64 group_id, job::Job j);     // 홈 샤드에서 실행
		void OnGroupMulticastRemote(uint64 group_id, job::Job j);   // 원격 샤드에서 실행

		// 유틸: 로컬 멤버에게 배달
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
		std::weak_ptr<GlobalExecutor>                       m_owner;         // Assist 요청 위해

		Atomic<bool>                                        m_running{ false };
		std::thread                                         m_thread;

		// per-shard FiberScheduler (큐와 독립)
		thrd::WinFiberBackend								m_backend;
		Uptr<thrd::FiberScheduler>                          m_scheduler;
		void*                                               m_mainFiber = nullptr;


		ShardSlot*											m_shardSlot = nullptr;

		// shard 큐 (MPSC 패턴)
		moodycamel::ConcurrentQueue<job::Job>               m_shardsQ;
		Uptr<moodycamel::ConsumerToken>						m_shardsCtok;
		// ready Mailbox 목록 (MPSC, Mailbox가 0→1 전이 시 push)
		moodycamel::ConcurrentQueue<Mailbox*>               m_readyCtrlQ;
		moodycamel::ConcurrentQueue<Mailbox*>				m_readyNormalQ;
		Uptr<moodycamel::ConsumerToken>						m_readyCtrlCtok;
		Uptr<moodycamel::ConsumerToken>						m_readyNormalCtok;

		// Mailbox 관리 (수명)
		USE_LOCK
		xmap<uint32, std::shared_ptr<Mailbox>>              m_mailboxes;
		Atomic<uint32>                                      m_nextMailboxId{ 1 };

		// Assist 과도 요청 억제
		Atomic<bool>                                        m_assistRequested{ false };

		// Shard Pinning
		bool												m_pinEnabled = false;
		utils::sys::CoreSlot								m_pinSlot = {};


		// 샤드-로컬 그룹 상태(내 샤드의 로컬 멤버 목록)
		std::unordered_map<uint64, GroupLocal>				m_groupLocal;
		// 홈 역할(내가 홈인 그룹들의 분포/메타)
		std::unordered_map<uint64, GroupHome>				m_groupHome;

		ShardLocal											m_local;
	};
}
