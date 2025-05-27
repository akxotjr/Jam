#pragma once
#include "Containers.h"
#include "ISingletonLayer.h"

namespace jam::utils::thread
{
	class DeadLockProfiler : public ISingletonLayer<DeadLockProfiler>
	{
	public:
		void									PushLock(const char* name);
		void									PopLock(const char* name);
		void									CheckCycle();

	private:
		void									Dfs(int32 here);

	private:
		std::unordered_map<const char*, int32>	m_nameToId;
		std::unordered_map<int32, const char*>	m_idToName;
		Map<int32, Set<int32>>					m_lockHistory;

		Mutex									m_lock;

	private:
		Vector<int32>							m_discoveredOrder;
		int32									m_discoveredCount = 0;
		Vector<bool>							m_finished;
		vector<int32>							m_parent;
	};
}

