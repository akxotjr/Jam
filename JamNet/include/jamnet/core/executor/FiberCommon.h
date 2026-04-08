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

}
