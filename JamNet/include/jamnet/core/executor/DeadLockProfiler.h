#pragma once


namespace jam
{
	class DeadLockProfiler
	{
		DECLARE_SINGLETON(DeadLockProfiler)

	public:
		void										PushLock(const char* name);
		void										PopLock(const char* name);
		void										CheckCycle();

	private:
		void										Dfs(int32 here);

	private:
		std::unordered_map<const char*, int32>		m_nameToId;
		std::unordered_map<int32, const char*>		m_idToName;
		std::map<int32, std::set<int32>>			m_lockHistory;

		Mutex										m_lock;

		std::vector<int32>							m_discoveredOrder;
		int32										m_discoveredCount = 0;
		std::vector<bool>							m_finished;
		std::vector<int32>							m_parent;
	};
}

