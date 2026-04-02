#pragma once
#include "jamnet/core/executor/FiberCommon.h"
#include "jamnet/core/executor/WinFiberBackend.h"

#include "concurrentqueue/concurrentqueue.h"

namespace jam
{
	class Fiber;

    struct FiberDesc
    {
        uint64          stackReserve    = 512 * 1024;
        uint64          stackCommit     = 128 * 1024;
        const char*     name            = nullptr;
        int32           priority        = 0;
        CancelToken*    cancelToken     = nullptr;
    };

	class FiberScheduler
	{
	public:
        explicit FiberScheduler(WinFiberBackend& backend);
		~FiberScheduler() = default;

		void                    AttachToCurrentThread();
		void                    DetachFromThread();

		uint32                  SpawnFiber(FiberFn fn, const FiberDesc& desc = {});
		void                    YieldFiber();
		void                    SleepUntil(uint64 wakeup_ns);
		bool                    Suspend(FiberAwaitKey key, uint64 deadline_ns); // true=정상, false=타임아웃/취소

        // inside
		bool                    Resume(FiberAwaitKey key);
        bool                    CancelByKey(FiberAwaitKey key, eCancelCode code = eCancelCode::Manual);
        bool                    CancelById(uint32 id, eCancelCode code = eCancelCode::Manual);

        // outside
		void                    PostResume(FiberAwaitKey key);
		void                    PostSpawn(FiberFn fn, const FiberDesc& desc = {});
        void                    PostCancelByKey(FiberAwaitKey key, eCancelCode code);
        void                    PostCancelById(uint32 id, eCancelCode code);


		void                    DrainInbox();
		void                    Poll(int32 budget, uint64 now_ns);
        void                    ResetProfile() { m_profile = {}; }

		uint32                  Current() const;

        const ProfileSample&    Profile() const { return m_profile; }


	private:
        struct TrampolineParam
        {
	        FiberScheduler*     self = nullptr;
        	uint32              id   = 0;
        };

        // Fiber Meta Data
        struct Fiber
		{
            uint32          id              = 0;
            const char*     name            = nullptr;
            void*           ctx             = nullptr;                // Win Fiber Handle
            uint64          reserve         = 0;
        	uint64          commit          = 0;

            eFiberState     state           = eFiberState::Ready;
            eResumeCode     resume          = eResumeCode::None;
            uint64          wakeup_ns       = 0;
            uint64          deadline_ns     = 0;
            FiberAwaitKey   awaitKey        = 0;
            bool            inReadyQ        = false;

            int32           priority        = 0;
            uint64          enqSequence     = 0;

            CancelToken*    cancel          = nullptr;
            uint64          switches        = 0;
            uint64          steps           = 0;
            uint64          runtimeAcc_ns   = 0;
            uint64          lastRunStart_ns = 0;

            FiberFn         entry           = nullptr;
            TrampolineParam param           = {};
            FlsFiberCtx     fls             = {};
        };

        struct ResumeMsg
        {
	        FiberAwaitKey    key     = 0;
        };

        struct SpawnMsg
        {
	        FiberFn         fn       = nullptr;
        	FiberDesc       desc     = {};
        };

        struct CancelKeyMsg
        {
            FiberAwaitKey   key      = 0;
            eCancelCode     code     = eCancelCode::None;
        };

        struct CancelIdMsg
        {
            uint32          id       = 0;
            eCancelCode     code     = eCancelCode::None;
        };

        struct ReadyItem
        {
            int32           priority = 0;
            uint64          seq      = 0;
            uint32          id       = 0; 
        };

        struct ReadyCmp
        {
	        bool operator()(const ReadyItem& a, const ReadyItem& b) const
	        {
                if (a.priority != b.priority)
                    return a.priority > b.priority;
                return a.seq > b.seq;
	        }
        };

        struct SleepItem
        {
            uint64          wakeup_ns = 0;
            uint32          fiberId   = 0;
        };

        struct SleepCmp
        {
	        bool operator()(const SleepItem& a, const SleepItem& b) const
	        {
                return a.wakeup_ns > b.wakeup_ns;
	        }
        };


        using FiberPool = ObjectPool<Fiber>;



        static VOID WINAPI          Trampoline(void* p);

        void                        MakeReady(uint32 id);
        void                        OnFiberException(uint32 id, const char* what);
        Fiber*                      CurrentFiber();
        void                        BindFls(Fiber* f);
        void                        StartRun(Fiber* f);
        void                        EndRun(Fiber* f);
        void                        CompleteAwait(Fiber* f, eResumeCode rc, eCancelCode cc = eCancelCode::None);

        void                        WakeupTimed(uint64 wakeup_ns);

	private:

        std::thread::id                                                     m_ownerThreadId;

        WinFiberBackend&                                                    m_backend;
        void*                                                               m_main      = nullptr;
        FlsFiberCtx                                                         m_mainCtx   = {};
        uint32                                                              m_currentId = 0;

        uint64                                                              m_readySeq  = 0;


        uint32                                                              m_nextId    = 1;
        std::unordered_map<uint32, Fiber*>                                  m_fibers;

        std::priority_queue<ReadyItem, std::vector<ReadyItem>, ReadyCmp>    m_readyPQ;
        std::priority_queue<SleepItem, std::vector<SleepItem>, SleepCmp>    m_sleepPQ;

        std::unordered_map<FiberAwaitKey, uint32>                           m_waitMap;

        // Inbox
        moodycamel::ConcurrentQueue<ResumeMsg>                              m_resumeInbox;
        moodycamel::ConcurrentQueue<SpawnMsg>                               m_spawnInbox;
        moodycamel::ConcurrentQueue<CancelKeyMsg>                           m_cancelKeyInbox;
        moodycamel::ConcurrentQueue<CancelIdMsg>                            m_cancelIdInbox;

        moodycamel::ConsumerToken                                           m_resumeCtok;
        moodycamel::ConsumerToken                                           m_spawnCtok;
        moodycamel::ConsumerToken                                           m_cancelKeyCtok;
        moodycamel::ConsumerToken                                           m_cancelIdCtok;

        ProfileSample                                                       m_profile   = {};

        // 기본 스택 크기
        static constexpr size_t kDefReserve = 512 * 1024;
        static constexpr size_t kDefCommit = 128 * 1024;
	};
}
