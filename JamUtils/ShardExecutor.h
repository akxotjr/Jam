#pragma once
#include "concurrentqueue/concurrentqueue.h"
#include "Mailbox.h"
#include "Job.h"
#include "NumaTopology.h"


namespace
{
	// 큐 주소별 TLS ProducerToken 캐시
	template <typename Q>
	moodycamel::ProducerToken& TlsTokenFor(Q& q)
	{
		static thread_local std::unordered_map<const void*, std::unique_ptr<moodycamel::ProducerToken>> s_tokens;
		void* key = static_cast<void*>(&q);
		auto it = s_tokens.find(key);
		if (it == s_tokens.end())
		{
			auto ins = s_tokens.emplace(key, std::make_unique<moodycamel::ProducerToken>(q));
			return *ins.first->second;
		}
		return *it->second;
	}

} // anonymous

namespace jam::utils::exec
{
	class GlobalExecutor; // fwd

	struct ShardExecutorConfig
	{
		int32		index = 0;
		int32		batchBudget = 32;    // Mailbox당 1회 처리량
		int32		idleSleepMs = 1;     // 유휴 슬립
		uint64		assistThreshold = 512; // Mailbox 길이 임계치

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

		// 샤드 내부용 주입 (옵션: 샤드 레벨 작업)
		void                        Submit(job::Job job);

		// Mailbox 관리
		std::shared_ptr<Mailbox>    CreateMailbox();
		void                        RemoveMailbox(uint32 id);

		// Global이 호출하는 보조 Drain
		void                        AssistDrainOnce(int32 maxMailboxes, int32 budgetPerMailbox);

		// Mailbox가 0→1 전이 시 호출
		void                        NotifyReady(Mailbox* mb);

		int32                       GetIndex() const { return m_config.index; }


		// NUMA / Shard Pinning
		void						SetPinSlot(const utils::sys::CoreSlot& slot);
		uint16						GetNumaNode() const { return m_config.numaNode; }

		// Fiber
		void						SpawnFiber(thrd::FiberFn fn, const thrd::FiberDesc& desc);
		void						ResumeFiber(thrd::AwaitKey key);
		void						CancelFiberByKey(thrd::AwaitKey key, thrd::eCancelCode code);
		void						CancelFiberById(uint32 id, thrd::eCancelCode code);


	private:
		void                        Loop();
		bool                        ProcessReadyOnce();
		void                        ProcessMailbox(Mailbox* mb, int32 budget);
		void                        RequestAssistIfNeeded(Mailbox* mb);

	private:
		ShardExecutorConfig                                 m_config{};
		std::weak_ptr<GlobalExecutor>                       m_owner;         // Assist 요청 위해

		Atomic<bool>                                        m_running{ false };
		std::thread                                         m_thread;

		// per-shard FiberScheduler (큐와 독립)
		thrd::WinFiberBackend								m_backend;
		Uptr<thrd::FiberScheduler>                          m_scheduler;
		void*                                               m_mainFiber = nullptr;

		// shard 큐 (MPSC 패턴)
		moodycamel::ConcurrentQueue<job::Job>               m_shardsQ;
		Uptr<moodycamel::ConsumerToken>						m_shardsCtok;
		// ready Mailbox 목록 (MPSC, Mailbox가 0→1 전이 시 push)
		moodycamel::ConcurrentQueue<Mailbox*>               m_readyQ;
		Uptr<moodycamel::ConsumerToken>						m_readyCtok;

		// Mailbox 관리 (수명)
		USE_LOCK
		xmap<uint32, std::shared_ptr<Mailbox>>              m_mailboxes;
		Atomic<uint32>                                      m_nextMailboxId{ 1 };

		// Assist 과도 요청 억제
		Atomic<bool>                                        m_assistRequested{ false };

		// Shard Pinning
		bool												m_pinEnabled = false;
		utils::sys::CoreSlot								m_pinSlot = {};
	};
}
