#pragma once


namespace jam
{
	class ThreadRegistry
	{
	public:
		static uint32		AllocateThreadID();
		static void			InitExecutorThread(string_view name, IExecutor* executor = nullptr);
		static string		GetCurrentThreadInfo();
	};
}