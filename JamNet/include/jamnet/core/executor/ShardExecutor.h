#pragma once
#include "jamnet/core/executor/Mailbox.h"
#include "jamnet/core/executor/Job.h"
#include "jamnet/core/executor/NumaTopology.h"
#include "jamnet/core/executor/ShardSlot.h"
#include "jamnet/core/executor/GlobalEventBus.h"
#include "jamnet/core/executor/ShardRoutingPolicy.h"
#include "jamnet/core/executor/FiberScheduler.h"
#include "jamnet/core/executor/ExecutorMetrics.h"
#include "jamnet/core/executor/ExecutorPeriodic.h"
#include "jamnet/core/executor/ShardDomain.h"
#include "jamnet/core/net/NetworkMetrics.h"

#include <condition_variable>
#include <deque>

#include "MailboxRef.h"

namespace jam::net
{
	struct SessionShardState;
	struct WorldShardState;
	struct UserShardState;
	struct RuntimePacketScratch;
}

namespace jam
{
	struct ShardLocal;

	struct ShardSystemGroup
	{
		using SystemFn    = std::function<void(ShardLocal&, Timestamp now, Duration dt)>;
		using BootstrapFn = std::function<void(ShardLocal&)>;


		ShardDomain		tag						   = {};
		Duration		tickPeriod				   = 0_ns;
		Timestamp		nextTick				   = Timestamp::Zero();

		std::vector<SystemFn>								systems;
		std::vector<BootstrapFn>							bootstraps;

		std::function<bool(entt::registry&, entt::entity)>	entityFilter   = nullptr;
	};


	struct ShardLocal
	{
		entt::registry										registry;
		uint32												shardIndex	 = UINT32_MAX;
		net::NetworkMetrics									networkMetrics;
		FiberScheduler*										scheduler    = nullptr;

		std::shared_ptr<net::SessionShardState>				sessionState = nullptr;
		std::shared_ptr<net::WorldShardState>				worldState	 = nullptr;
		std::shared_ptr<net::UserShardState>				usersState	 = nullptr;
		std::shared_ptr<net::RuntimePacketScratch>			packetScratch = nullptr;

		std::vector<std::function<void(entt::registry&)>>	defers;

		std::unordered_map<ShardDomain, ShardSystemGroup>	domainGroups;
	};


	inline void RegisterShardSystemFn(ShardLocal& L, ShardDomain tag, Duration tickPeriod, ShardSystemGroup::SystemFn system)
	{
		auto& group = L.domainGroups[tag];
		group.tag				   = tag;
		group.tickPeriod		   = tickPeriod;

		group.systems.push_back(std::move(system));
	}

	inline void RegisterShardBootstrapFn(ShardLocal& L, ShardDomain tag, ShardSystemGroup::BootstrapFn bootstrap)
	{
		auto& group = L.domainGroups[tag];
		group.tag = tag;
		group.bootstraps.push_back(std::move(bootstrap));
	}




	struct ShardExecutorConfig
	{
		int32		index                              = 0;
		int32		ingressMoveBudgetPerLoop           = 64;
		int32		localExecuteBudgetPerPreTickPass   = 64;
		int32		localRecoveryBudgetAfterTick       = 128;
		int32		mailboxServiceBudgetPerLoop        = 128;
		int32		mailboxMoveBudgetPerLoop           = 4096;
		int32		mailboxMoveBudgetPerMailbox        = 64;
		// Additional service passes run between complete domain ticks and after Poll.
		int32		mailboxServiceBudgetPerBoundary    = 8;
		int32		mailboxMoveBudgetPerBoundary       = 64;
		int32		mailboxMoveBudgetPerBoundaryMailbox = 8;
		int32		localExecuteBudgetPerBoundary      = 64;
		Duration	mailboxServiceTimeBudgetPerBoundary = 200_us;
		int32		schedulerPollBudgetPerLoop         = 64;
		Duration	idleSleepMs						   = 1_ms;
		uint16		numaNode						   = 0xFFFF;
		int32		maxTickCatchUp					   = 4;
	};



	class ShardExecutor : public std::enable_shared_from_this<ShardExecutor>, public IExecutor
	{
	public:
		explicit ShardExecutor(const ShardExecutorConfig& config = {});
		~ShardExecutor() override;

		void							Start();
		void							Stop();
		void							Join();

		void							AttachSlot(ShardSlot* slot) { m_shardSlot = slot; }

		void							Submit(Job j) override;
		void							SubmitAfter(Job j, Duration delay);

		MailboxRef						CreateMailboxRef(RuntimeId ownerId);
		bool							CloseMailbox(uint32 id, eMailboxCloseMode mode = eMailboxCloseMode::Drain, std::function<void()> onClosed = {});
		void							RemoveMailbox(uint32 id);
		bool							NotifyReady(uint32 mailboxId);

		void							BeginDrain();

		void							SpawnFiber(FiberFn fn, const FiberDesc& desc);
		void							ResumeFiber(FiberAwaitKey key);
		void							ResumeFiber(FiberAwaitKey key, int32 readyPriority);
		void							CancelFiberByKey(FiberAwaitKey key, eCancelCode code);
		void							CancelFiberById(uint32 id, eCancelCode code);
		FiberAwaitKey					AllocateAwaitKey();

		PeriodicHandle					ScheduleFixedRate(Job j, const PeriodicOptions& opt);
		PeriodicHandle					ScheduleFixedDelay(Job j, const PeriodicOptions& opt);
		bool							CancelPeriodic(PeriodicHandle h);

		int32							GetIndex() const { return m_config.index; }
		ShardLocal&						Local() { return m_local; }
		const ShardLocal&				Local() const { return m_local; }

		size_t							GetQueueSize() const { return m_jobIngress.size_approx() + m_jobLocalTotal.load(std::memory_order_relaxed); }

		void							PinCoreSlot(const CoreSlot& slot, uint16 numaNode = 0xFFFF);
		uint16							GetNumaNode() const { return m_config.numaNode; }
	private:
		void							UpdateMetricsWindow(Timestamp now, Duration loopDuration, uint64 ingressMoved, uint64 mailboxMoved, uint64 jobsExecuted);
		void							SubmitMetricsWindow(Timestamp windowEnd);
		void							ResetMetricsWindow();

		void							Loop();
		bool							RunDueDomainGroups(Timestamp now);
		bool							ProcessDefers();
		Duration						TimeUntilNextDomainDue(Timestamp now) const;
		void							NotifyWorkAvailable();
		void							WaitForWorkOrTimeout(uint64 observedWakeEpoch, Duration timeout);

		void							PushLocal(Job&& j);
		bool							TryPopLocal(OUT Job& j);
		bool							DrainIngressOnce(int32 budget);

		bool							ProcessJobsOnce(int32 executeBudget, bool drainIngress = true, Timestamp deadline = {});
		uint64							ProcessMailbox(Mailbox* mb, int32 budget);
		void							DrainReadyMailboxes(int32 maxMailboxes, int32 totalJobBudget, int32 budgetPerMailbox, Timestamp deadline = {});
		bool							ServiceMailboxesAtBoundary();
		Mailbox*						CreateMailbox();
		Mailbox*						FindMailbox(uint32 id);

	private:
		ShardExecutorConfig										m_config  = {};

		std::atomic<bool>										m_running = false;
		std::thread												m_thread;

		std::atomic<uint64>										m_nextAwaitSeq = 1;
		WinFiberBackend											m_backend;
		std::unique_ptr<FiberScheduler>							m_scheduler;

		ShardSlot*												m_shardSlot	= nullptr;

		ConcurrentQueue<Job>									m_jobIngress;
		std::atomic<uint64>										m_ingressJobSubmitCount = 0;
		std::deque<Job>											m_jobLocalCritical = {};
		std::deque<Job>											m_jobLocalControl  = {};
		std::deque<Job>											m_jobLocalBackground = {};
		std::atomic<size_t>										m_jobLocalTotal		= 0;
		uint32													m_consecutiveControlPops = 0;

		USE_LOCK
		std::unordered_map<uint32, std::unique_ptr<Mailbox>>    m_mailboxes;
		std::atomic<uint32>										m_nextMailboxId	 = 1;
		struct ReadyMailboxEntry
		{
			uint32	  mailboxId		= 0;
			Timestamp enqueuedAt = Timestamp::Zero();
		};
		ConcurrentQueue<ReadyMailboxEntry>						m_readyMailboxes;
		std::mutex												m_wakeMutex;
		std::condition_variable									m_wakeCv;
		std::atomic<uint64>										m_wakeEpoch		 = 0;

		std::atomic<bool>										m_assistRequested = false;

		// Shard Pinning
		bool													m_pinEnabled = false;
		CoreSlot												m_pinSlot	 = {};

		ShardLocal												m_local;

		// Periodic
		struct PeriodicState
		{
			std::atomic<bool>   cancelled		= false;
			Duration			period			= 0_ns;
			Duration			initialDelay	= 0_ns;
			int32				maxCatchUp		= 0;
			bool				fixedRate		= true;
			FiberAwaitKey		awaitKey		= 0;
		};

		std::atomic<uint32>											m_periodicId		= 1;
		std::unordered_map<uint32, std::weak_ptr<PeriodicState>>	m_periodics;

		bool																m_mailboxBudgetExhaustedThisLoop = false;
		ShardExecutorMetrics										m_metrics			= {};
		uint64														m_metricsWindowIndex = UINT64_MAX;
		Timestamp													m_metricsWindowStart_ns = Timestamp::Zero();
	};


} // namespace jam
