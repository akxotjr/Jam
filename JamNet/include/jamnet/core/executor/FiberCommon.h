#pragma once


namespace jam
{
	using FiberAwaitKey = uint64;	
	using FiberFn		= std::function<void()>;

	enum class eFiberState : uint8
	{
		Ready,
		WatingTimer,
		WatingExternal,
		Terminated
	};

	enum class eResumeCode : uint8
	{
		None,
		Signaled,
		Timeout,
		Cancelled
	};

	enum class eCancelCode : uint8
	{
		None,
		Manual,
		Timeout,
		Shutdown
	};


	class CancelToken
	{
	public:
		void RequestCancel(eCancelCode code = eCancelCode::Manual)
		{
			m_cancelCode.store(static_cast<int>(code), std::memory_order_relaxed);
			m_cancelled.store(true, std::memory_order_release);
		}
		bool			IsCancelled()   const { return m_cancelled.load(std::memory_order_acquire); }
		eCancelCode		GetCancelCode() const { return static_cast<eCancelCode>(m_cancelCode.load(std::memory_order_relaxed)); }
	private:
		std::atomic<bool>	m_cancelled{ false };
		std::atomic<int>	m_cancelCode{ static_cast<int32>(eCancelCode::None) };
	};

	
	struct ProfileSample
	{
        uint64 stepCount				= 0;
		uint64 switchCount				= 0;

		uint64 pollCount				= 0;
		uint64 emptyPollCount			= 0;
		uint64 pollCostAcc_ns			= 0;
		uint64 lastPollCost_ns			= 0;

		uint64 readyRunCount			= 0;
        uint64 lastPollReadyRunCount	= 0;
		uint64 inboxResumeCount			= 0;
		uint64 inboxSpawnCount			= 0;
		uint64 inboxCancelByKeyCount	= 0;
		uint64 inboxCancelByIdCount		= 0;

		uint64 wakeupTimerCount			= 0;
		uint64 wakeupTimeoutCount		= 0;
	};
}
