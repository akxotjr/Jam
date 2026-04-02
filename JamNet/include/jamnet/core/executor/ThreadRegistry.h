#pragma once


namespace jam
{
	class ThreadRegistry
	{
	public:
		static uint32			AllocateThreadID();
		static void				InitExecutorThread(std::string_view name, IExecutor* executor = nullptr);
		static std::string		GetCurrentThreadInfo();
	};
}