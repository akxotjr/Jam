#pragma once


namespace jam::utils::thrd
{
	using AwaitKey = uint64;	// todo: change to struct not type alias
	using FiberFn = std::function<void()>;

	enum class eFiberState : uint8
	{
		READY,
		WAITING_TIMER,
		WAITING_EXTERNAL,
		TERMINATED
	};

	enum class eResumeCode : uint8
	{
		NONE,
		SIGNALED,
		TIMEOUT,
		CANCELLED
	};

	enum class eCancelCode : uint8
	{
		NONE,
		MANUAL,
		TIMEOUT,
		SHUTDOWN
	};


	class CancelToken
	{
	public:
		void RequestCancel(eCancelCode code = eCancelCode::MANUAL)
		{
			m_cancelCode.store(static_cast<int>(code), std::memory_order_relaxed);
			m_cancelled.store(true, std::memory_order_release);
		}
		bool			IsCancelled() const { return m_cancelled.load(std::memory_order_acquire); }
		eCancelCode		GetCancelCode() const { return static_cast<eCancelCode>(m_cancelCode.load(std::memory_order_relaxed)); }
	private:
		std::atomic<bool> m_cancelled{ false };
		std::atomic<int>  m_cancelCode{ static_cast<int>(eCancelCode::NONE) };
	};

	// 프로파일 샘플
	struct ProfileSample
	{
		uint64 stepCount = 0;
		uint64 switchCount = 0;
		uint64 lastPollCost_ns = 0;
	};
}
