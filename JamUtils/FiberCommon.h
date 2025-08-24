#pragma once


namespace jam::utils::thrd
{
	using AwaitKey = uint64;
	using FiberFn = std::function<void()>;

	enum class eFiberState
	{
		READY,
		WAITING_TIMER,
		WAITING_EXTERNAL,
		TERMINATED
	};

	enum class eResumeCode
	{
		NONE,
		SIGNALED,
		TIMEOUT
	};
}
