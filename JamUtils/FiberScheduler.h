#pragma once
#include "FiberCommon.h"
#include "WinFiberBackend.h"
#include "concurrentqueue/concurrentqueue.h"

namespace jam::utils::thrd
{
	class Fiber;

    struct FiberDesc
    {
        uint64      stackReserve = 0;
        uint64      stackCommit = 0;
        const char* name = nullptr;
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
		bool        Suspend(AwaitKey key, uint64 deadline);

		bool        Resume(AwaitKey key);

		void        PostResume(AwaitKey key);
		void        PostSpawn(FiberFn fn, FiberDesc desc = {});

		void        DrainInbox();
		void        Poll(int32 budget, uint64 now);

		uint32      Current() const;


	private:
        struct TrampolineParam
        {
	        FiberScheduler*     self;
        	uint32              id;
        };

        struct Fiber
		{
            uint32          id = 0;
            const char*     name = nullptr;
            void*           ctx = nullptr;                // Win Fiber 핸들
            uint64          reserve = 0;
        	uint64          commit = 0;

            eFiberState     state = eFiberState::READY;
            eResumeCode     resume = eResumeCode::NONE;
            uint64_t        wakeupTick = 0;
            uint64_t        deadline = 0;
            AwaitKey        awaitKey = 0;

            FiberFn         entry = nullptr;
            TrampolineParam param = {};
            FlsFiberCtx     fls = {};
        };

        struct ResumeMsg
        {
	        AwaitKey key;
        };

        struct SpawnMsg
        {
	        FiberFn fn;
        	FiberDesc desc;
        };

        static VOID WINAPI  Trampoline(void* p);        
        void                MakeReady(uint32_t id);
        void                OnFiberException(uint32_t id, const char* what);

        // Helpers
        Fiber*              CurrentFiber();
        void                BindFls(Fiber* f);

	private:
        WinFiberBackend&            m_backend;
        void*                       m_main = nullptr;
        FlsFiberCtx                 m_mainCtx = {};
        uint32                      m_currentId = 0;


        uint32                                              m_nextId = 1;
        std::unordered_map<uint32, std::unique_ptr<Fiber>>  m_fibers;
        std::deque<uint32_t>                                m_readyQ;

        // (wakeupTick, fiberId)
        using SleepNode = std::pair<uint64_t, uint32_t>;
        struct Cmp { bool operator()(const SleepNode& a, const SleepNode& b) const { return a.first > b.first; } };
        std::priority_queue<SleepNode, std::vector<SleepNode>, Cmp> m_sleepQ;

        std::unordered_map<AwaitKey, uint32_t>      m_waitMap;

        // Inbox
        moodycamel::ConcurrentQueue<ResumeMsg>      m_resumeInbox;
        moodycamel::ConcurrentQueue<SpawnMsg>       m_spawnInbox;
        std::unique_ptr<moodycamel::ConsumerToken>  m_resumeCtok;
        std::unique_ptr<moodycamel::ConsumerToken>  m_spawnCtok;

        // 기본 스택 크기
        static constexpr size_t kDefReserve = 512 * 1024;
        static constexpr size_t kDefCommit = 128 * 1024;
	};
}
