#pragma once
#include "FiberCommon.h"
#include "WinFiberBackend.h"
#include "concurrentqueue/concurrentqueue.h"

namespace jam::utils::thrd
{
	class Fiber;

    struct FiberDesc
    {
        uint64          stackReserve = 0;
        uint64          stackCommit = 0;
        const char*     name = nullptr;
        int32           priority = 0;
        CancelToken*    cancelToken = nullptr;
    };

	class FiberScheduler
	{
	public:
		explicit FiberScheduler(WinFiberBackend& backend) : m_backend(backend) {}
		~FiberScheduler() = default;

		void        AttachToCurrentThread();
		void        DetachFromThread();

		uint32      SpawnFiber(FiberFn fn, const FiberDesc& desc = {});
		void        YieldFiber();
		void        SleepUntil(uint64 tick);
		bool        Suspend(AwaitKey key, uint64 deadline); // true=정상, false=타임아웃/취소

        // inside
		bool        Resume(AwaitKey key);
        bool        CancelByKey(AwaitKey key, eCancelCode code = eCancelCode::MANUAL);
        bool        CancelById(uint32 id, eCancelCode code = eCancelCode::MANUAL);

        // outside
		void        PostResume(AwaitKey key);
		void        PostSpawn(FiberFn fn, FiberDesc desc = {});
        void        PostCancelByKey(AwaitKey key, eCancelCode code);
        void        PostCancelById(uint32 id, eCancelCode code);


		void        DrainInbox();
		void        Poll(int32 budget, uint64 now);

		uint32      Current() const;

        const ProfileSample& Profile() const { return m_profile; }


	private:
        struct TrampolineParam
        {
	        FiberScheduler*     self;
        	uint32              id;
        };

        // Fiber Meta Data
        struct Fiber
		{
            uint32          id = 0;
            const char*     name = nullptr;
            void*           ctx = nullptr;                // Win Fiber 핸들
            uint64          reserve = 0;
        	uint64          commit = 0;

            eFiberState     state = eFiberState::READY;
            eResumeCode     resume = eResumeCode::NONE;
            uint64          wakeupTick = 0;
            uint64          deadline = 0;
            AwaitKey        awaitKey = 0;

            int32           priority = 0;
            uint64          enqSequence = 0;

            CancelToken*    cancel = nullptr;
            uint64          switches = 0;
            uint64          steps = 0;
            uint64          runNsAcc = 0;
            uint64          lastRunStartNs = 0;

            FiberFn         entry = nullptr;
            TrampolineParam param = {};
            FlsFiberCtx     fls = {};
        };

        struct ResumeMsg
        {
	        AwaitKey    key;
        };

        struct SpawnMsg
        {
	        FiberFn     fn;
        	FiberDesc   desc;
        };

        struct CancelKeyMsg
        {
            AwaitKey    key;
            eCancelCode code;
        };

        struct CancelIdMsg
        {
            uint32      id;
            eCancelCode code;
        };

        struct ReadyItem
        {
            int32       priority;
            uint64      seq;
            uint32      id; 
        };

        struct ReadyCmp
        {
	        bool operator()(const ReadyItem& a, const ReadyItem& b) const
	        {
                if (a.priority != b.priority)
                    return a.priority > b.priority;
                return a.seq > b.seq;   // FIFO
	        }
        };

        struct SleepItem
        {
            uint64 wakeupTick;
            uint32 fiberId;
        };

        struct SleepCmp
        {
	        bool operator()(const SleepItem& a, const SleepItem& b) const
	        {
                return a.wakeupTick > b.wakeupTick;
	        }
        };


        using FiberPool = jam::utils::memory::ObjectPool<Fiber>;






        static VOID WINAPI          Trampoline(void* p);

        void                        MakeReady(uint32 id);
        void                        OnFiberException(uint32 id, const char* what);
        Fiber*                      CurrentFiber();
        void                        BindFls(Fiber* f);
        void                        StartRun(Fiber* f);
        void                        EndRun(Fiber* f);
        void                        Wake(Fiber* f, eResumeCode rc, eCancelCode cc = eCancelCode::NONE);

	private:

        WinFiberBackend&            m_backend;
        void*                       m_main = nullptr;
        FlsFiberCtx                 m_mainCtx = {};
        uint32                      m_currentId = 0;

        uint64                      m_readySeq = 0;



        uint32                                              m_nextId = 1;
        std::unordered_map<uint32, Fiber*>                  m_fibers;

        std::priority_queue<ReadyItem, std::vector<ReadyItem>, ReadyCmp> m_readyPQ;
        std::priority_queue<SleepItem, std::vector<SleepItem>, SleepCmp> m_sleepPQ;

        std::unordered_map<AwaitKey, uint32_t>      m_waitMap;

        // Inbox
        moodycamel::ConcurrentQueue<ResumeMsg>      m_resumeInbox;
        moodycamel::ConcurrentQueue<SpawnMsg>       m_spawnInbox;
        moodycamel::ConcurrentQueue<CancelKeyMsg>   m_cancelKeyInbox;
        moodycamel::ConcurrentQueue<CancelIdMsg>    m_cancelIdInbox;

        std::unique_ptr<moodycamel::ConsumerToken>  m_resumeCtok;
        std::unique_ptr<moodycamel::ConsumerToken>  m_spawnCtok;
        std::unique_ptr<moodycamel::ConsumerToken>  m_cancelKeyCtok;
        std::unique_ptr<moodycamel::ConsumerToken>  m_cancelIdCtok;

        ProfileSample m_profile = {};

        // 기본 스택 크기
        static constexpr size_t kDefReserve = 512 * 1024;
        static constexpr size_t kDefCommit = 128 * 1024;
	};
}
