#pragma once
#include "Containers.h"
#include "ISingletonLayer.h"

namespace jam::utils::thrd
{
	class DeadLockProfiler : public ISingletonLayer<DeadLockProfiler>
	{
		friend class jam::ISingletonLayer<DeadLockProfiler>;

	public:
		void										PushLock(const char* name);
		void										PopLock(const char* name);
		void										CheckCycle();

	private:
		void										Dfs(int32 here);

	private:
		std::unordered_map<const char*, int32>		m_nameToId;
		std::unordered_map<int32, const char*>		m_idToName;
		xmap<int32, xset<int32>>					m_lockHistory;

		Mutex										m_lock;

	private:
		xvector<int32>								m_discoveredOrder;
		int32										m_discoveredCount = 0;
		xvector<bool>								m_finished;
		xvector<int32>								m_parent;
	};
}

