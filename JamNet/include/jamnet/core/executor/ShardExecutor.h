#pragma once
#include "Mailbox.h"
#include "Job.h"
#include "NumaTopology.h"
#include "ShardSlot.h"
#include "GlobalEventBus.h"
#include "RoutingPolicy.h"
#include "FiberScheduler.h"
#include "ConcurrentPriorityQueue.h"
#include "ShardDomain.h"

namespace jam
{
	struct ShardLocal;

	struct ShardSystemGroup
	{
		ShardDomain										tag{};
		uint64											tickPeriod_ns = 0;      // 이 그룹의 Tick 주기
		uint64											lastTick_ns = 0;

		// 시스템 함수들
		using SystemFn = std::function<void(ShardLocal&, uint64 now_ns, uint64 dt_ns)>;
		vector<SystemFn>								systems;

		// 초기화 함수 (한 번만 실행)
		using BootstrapFn = std::function<void(ShardLocal&)>;
		vector<BootstrapFn>								bootstraps;

		// 이 그룹의 시스템들이 처리할 엔티티 타입
		function<bool(entt::registry&, entt::entity)>	entityFilter;
	};


	// ============================================================
	//  ShardLocal
	// ============================================================

	struct ShardLocal
	{
		entt::registry										registry;
		FiberScheduler*										scheduler = nullptr;

		vector<function<void(entt::registry&)>>				defers;

		unordered_map<ShardDomain, ShardSystemGroup>		domainGroups;
	};


	inline void RegisterShardSystemFn(ShardLocal& L, ShardDomain tag, ShardSystemGroup::SystemFn system)
	{
		auto& group = L.domainGroups[tag];
		group.tag = tag;
		group.systems.push_back(std::move(system));
	}

	inline void RegisterShardBootstrapFn(ShardLocal& L, ShardDomain tag, ShardSystemGroup::BootstrapFn bootstrap)
	{
		auto& group = L.domainGroups[tag];
		group.tag = tag;
		group.bootstraps.push_back(std::move(bootstrap));
	}




	// ============================================================
	// RouteHome
	// ============================================================

	struct RouteHome
	{
		shared_ptr<Mailbox> mb;
	};


	// ============================================================
	// ShardExecutorConfig
	// ============================================================

	struct ShardExecutorConfig
	{
		int32       index			= 0;
		int32       batchBudget		= 32;
		int32       idleSleepMs		= 1;
		uint16      numaNode		= 0xFFFF;
		uint64      tickPeriod_ns	= 1'000'000_ns;
		int32		maxTickCatchUp  = 4;
	};


	// ============================================================
	// ShardExecutor
	// ============================================================

	class ShardExecutor : public std::enable_shared_from_this<ShardExecutor>, IExecutor
	{
	public:
		explicit ShardExecutor(const ShardExecutorConfig& config = {});
		~ShardExecutor() override;

		void                        Start();
		void                        Stop();
		void                        Join();

		void                        AttachSlot(ShardSlot* slot) { m_shardSlot = slot; }


		/// 샤드 전용 작업 등록 (우선순위 지정 가능)
		void                        Submit(Job j) override;
		void                        SubmitAfter(Job j, uint64 delay_ns);

		// ============================================================
		// Mailbox 
		// ============================================================

		shared_ptr<Mailbox>			CreateMailbox();
		void                        RemoveMailbox(uint32 id);
		void                        NotifyReady(Mailbox* mb);

		void                        BeginDrain();

		// ============================================================
		// Fiber (변경 없음)
		// ============================================================

		void                        SpawnFiber(FiberFn fn, const FiberDesc& desc);
		void                        ResumeFiber(AwaitKey key);
		void                        CancelFiberByKey(AwaitKey key, eCancelCode code);
		void                        CancelFiberById(uint32 id, eCancelCode code);


		// ============================================================
		// Periodic (변경 없음)
		// ============================================================

		struct PeriodicHandle { uint32 id = 0; };
		struct PeriodicOptions
		{
			uint64      period_ns = 0;
			uint64      initialDelay_ns = 0;
			int32       maxCatchUp = 0;
			const char* name = "Shard.Periodic";
		};

		PeriodicHandle              ScheduleFixedRate(Job j, const PeriodicOptions& opt);
		PeriodicHandle              ScheduleFixedDelay(Job j, const PeriodicOptions& opt);
		bool                        CancelPeriodic(PeriodicHandle h);

		// ============================================================
		// Accessors
		// ============================================================

		int32                       GetIndex() const { return m_config.index; }
		ShardLocal&					Local() { return m_local; }
		const ShardLocal&			Local() const { return m_local; }

		void                        PinCoreSlot(const CoreSlot& slot);
		uint16                      GetNumaNode() const { return m_config.numaNode; }

	private:
		void                        Loop();
		void                        Tick(uint64 now_ns, uint64 dt_ns);

		void						PushLocal(Job&& j);
		bool						TryPopLocal(OUT Job& j);
		bool						DrainIngressOnce(int32 budget);

		bool                        ProcessJobsOnce();
		void                        ProcessMailbox(Mailbox* mb, int32 budget);
		void						DrainReadyMailboxes(int32 maxMailboxes, int32 budgetPerMailbox);

	private:
		ShardExecutorConfig								m_config{};

		atomic<bool>                                    m_running{ false };
		thread                                          m_thread;

		atomic<uint64>                                  m_nextAwaitSeq{ 1 };
		WinFiberBackend                                 m_backend;
		unique_ptr<FiberScheduler>                      m_scheduler;

		ShardSlot*										m_shardSlot = nullptr;

		ConcurrentQueue<Job>							m_jobIngress;
		
		static constexpr size_t							kPrioCount = static_cast<size_t>(eJobPriority::COUNT);
		array<vector<Job>, kPrioCount>					m_jobLocalByPrio{};
		size_t											m_jobLocalTotal = 0;

		USE_LOCK
		unordered_map<uint32, shared_ptr<Mailbox>>      m_mailboxes;
		atomic<uint32>                                  m_nextMailboxId{ 1 };
		ConcurrentQueue<Mailbox*>						m_readyMailboxes;

		unordered_map<RouteKey, RouteHome>              m_routeHome;    // RouteKey -> Mailbox

		atomic<bool>                                    m_assistRequested{ false };

		// Shard Pinning
		bool                                            m_pinEnabled = false;
		CoreSlot                                        m_pinSlot = {};

		ShardLocal										m_local;

		uint64                                          m_lastTick_ns = 0_ns;

		// Periodic
		struct PeriodicState
		{
			atomic<bool>    cancelled{ false };
			uint64          period_ns = 0;
			uint64          initialDelay_ns = 0;
			int32           maxCatchUp = 0;
			bool            fixedRate = true;
		};

		atomic<uint32>                                  m_periodicId{ 1 };
		unordered_map<uint32, weak_ptr<PeriodicState>>	m_periodics;
	};
} // namespace jam