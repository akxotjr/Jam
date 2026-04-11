#pragma once
#include "jamnet/core/executor/Mailbox.h"
#include "jamnet/core/executor/Job.h"
#include "jamnet/core/executor/NumaTopology.h"
#include "jamnet/core/executor/ShardSlot.h"
#include "jamnet/core/executor/GlobalEventBus.h"
#include "jamnet/core/executor/RoutingPolicy.h"
#include "jamnet/core/executor/FiberScheduler.h"
#include "jamnet/core/executor/ExecutorMetrics.h"
#include "jamnet/core/executor/ShardDomain.h"
#include <condition_variable>

namespace jam
{
	struct ShardLocal;

	struct ShardSystemGroup
	{
		ShardDomain											tag			  = {};
		uint64												tickPeriod_ns = 0_ns;      // 이 그룹의 Tick 주기
		uint64												lastTick_ns   = 0_ns;

		// 시스템 함수들
		using SystemFn = std::function<void(ShardLocal&, uint64 now_ns, uint64 dt_ns)>;
		std::vector<SystemFn>								systems;

		// 초기화 함수 (한 번만 실행)
		using BootstrapFn = std::function<void(ShardLocal&)>;
		std::vector<BootstrapFn>							bootstraps;

		// 이 그룹의 시스템들이 처리할 엔티티 타입
		std::function<bool(entt::registry&, entt::entity)>	entityFilter   = nullptr;
	};


	// ============================================================
	//  ShardLocal
	// ============================================================

	struct ShardLocal
	{
		entt::registry										registry;
		FiberScheduler*										scheduler = nullptr;

		std::vector<std::function<void(entt::registry&)>>	defers;

		std::unordered_map<ShardDomain, ShardSystemGroup>	domainGroups;
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
		std::shared_ptr<Mailbox> mb;
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

		void							Start();
		void							Stop();
		void							Join();

		void							AttachSlot(ShardSlot* slot) { m_shardSlot = slot; }

		void							Submit(Job j) override;
		void							SubmitAfter(Job j, uint64 delay_ns);


		std::shared_ptr<Mailbox>		CreateMailbox();
		bool							CloseMailbox(uint32 id, eMailboxCloseMode mode = eMailboxCloseMode::Drain, std::function<void()> onClosed = {});
		void							RemoveMailbox(uint32 id);
		void							NotifyReady(uint32 mailboxId);

		void							BeginDrain();

		void							SpawnFiber(FiberFn fn, const FiberDesc& desc);
		void							ResumeFiber(FiberAwaitKey key);
		void							CancelFiberByKey(FiberAwaitKey key, eCancelCode code);
		void							CancelFiberById(uint32 id, eCancelCode code);



		struct PeriodicHandle { uint32 id = 0; };
		struct PeriodicOptions
		{
			uint64      period_ns		= 0_ns;
			uint64      initialDelay_ns = 0_ns;
			int32       maxCatchUp		= 0;
			const char* name			= "Shard.Periodic";
		};

		PeriodicHandle					ScheduleFixedRate(Job j, const PeriodicOptions& opt);
		PeriodicHandle					ScheduleFixedDelay(Job j, const PeriodicOptions& opt);
		bool							CancelPeriodic(PeriodicHandle h);

		// ============================================================
		// Accessors
		// ============================================================

		int32							GetIndex() const { return m_config.index; }
		ShardLocal&						Local() { return m_local; }
		const ShardLocal&				Local() const { return m_local; }

		size_t							GetQueueSize() const { return m_jobIngress.size_approx() + m_jobLocalTotal.load(std::memory_order_relaxed);
		}

		void							PinCoreSlot(const CoreSlot& slot);
		uint16							GetNumaNode() const { return m_config.numaNode; }
		ShardExecutorMetrics			Profile() const;
		void							ResetMetrics();

	private:
		void							ResetMetricsUnsafe();
		bool							IsShardThread() const;
		void							WaitUntilLoopExited() const;

		void							Loop();
		void							Tick(uint64 now_ns, uint64 dt_ns);

		void							PushLocal(Job&& j);
		bool							TryPopLocal(OUT Job& j);
		bool							DrainIngressOnce(int32 budget);

		bool							ProcessJobsOnce();
		void							ProcessMailbox(const std::shared_ptr<Mailbox>& mb, int32 budget);
		void							DrainReadyMailboxes(int32 maxMailboxes, int32 budgetPerMailbox);
		std::shared_ptr<Mailbox>		FindMailbox(uint32 id);

	private:
		ShardExecutorConfig								m_config{};

		std::atomic<bool>                               m_running{ false };
		std::thread                                     m_thread;

		std::atomic<uint64>                             m_nextAwaitSeq{ 1 };
		WinFiberBackend                                 m_backend;
		std::unique_ptr<FiberScheduler>                 m_scheduler;

		ShardSlot*										m_shardSlot = nullptr;

		ConcurrentQueue<Job>							m_jobIngress;
		
		static constexpr size_t							kPrioCount = static_cast<size_t>(eJobPriority::Count);
		std::array<std::vector<Job>, kPrioCount>		m_jobLocalByPrio{};
		std::atomic<size_t>								m_jobLocalTotal = 0;

		USE_LOCK
		std::unordered_map<uint32, std::shared_ptr<Mailbox>>      m_mailboxes;
		std::atomic<uint32>                             m_nextMailboxId{ 1 };
		ConcurrentQueue<uint32>							m_readyMailboxes;

		std::unordered_map<RouteKey, RouteHome>         m_routeHome;    // RouteKey -> Mailbox

		std::atomic<bool>                               m_assistRequested{ false };

		// Shard Pinning
		bool                                            m_pinEnabled = false;
		CoreSlot                                        m_pinSlot	 = {};

		ShardLocal										m_local;

		uint64                                          m_lastTick_ns = 0_ns;

		// Periodic
		struct PeriodicState
		{
			std::atomic<bool>   cancelled		= false;
			uint64				period_ns		= 0_ns;
			uint64				initialDelay_ns = 0_ns;
			int32				maxCatchUp		= 0;
			bool				fixedRate		= true;
		};

		std::atomic<uint32>											m_periodicId{ 1 };
		std::unordered_map<uint32, std::weak_ptr<PeriodicState>>	m_periodics;

		mutable std::mutex											m_metricSyncMutex;
		mutable std::condition_variable								m_metricSyncCv;
		mutable bool												m_loopExited = true;
		ShardExecutorMetrics										m_metrics = {};
	};


} // namespace jam
