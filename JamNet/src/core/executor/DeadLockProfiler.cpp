#include "pch.h"
#include "jamnet/core/executor/DeadLockProfiler.h"
#include "jamnet/core/executor/ThreadContext.h"

namespace jam
{

	void DeadLockProfiler::PushLock(const char* name)
	{
		LockGuard guard(m_lock);

		int32 lockId = 0;

		auto findIt = m_nameToId.find(name);
		if (findIt == m_nameToId.end())
		{
			lockId = static_cast<int32>(m_nameToId.size());
			m_nameToId[name]   = lockId;
			m_idToName[lockId] = name;
		}
		else
		{
			lockId = findIt->second;
		}

		auto& lockStack = CurrentThreadContext().lockStack;
		if (lockStack.empty() == false)
		{
			const int32 prevId = lockStack.top();
			if (lockId != prevId)
			{
				std::set<int32>& history = m_lockHistory[prevId];
				if (!history.contains(lockId))
				{
					history.insert(lockId);
					CheckCycle();
				}
			}
		}

		lockStack.push(lockId);
	}

	void DeadLockProfiler::PopLock(const char* name)
	{
		std::scoped_lock guard(m_lock);

		auto& lockStack = CurrentThreadContext().lockStack;
		if (lockStack.empty())
			JAM_CRASH("MULTIPLE_UNLOCK");

		int32 lockId = m_nameToId[name];
		if (lockStack.top() != lockId)
			JAM_CRASH("INVALID_UNLOCK");

		lockStack.pop();
	}

	void DeadLockProfiler::CheckCycle()
	{
		const int32 lockCount = static_cast<int32>(m_nameToId.size());
		m_discoveredOrder = std::vector<int32>(lockCount, -1);
		m_discoveredCount = 0;
		m_finished		  = std::vector<bool>(lockCount, false);
		m_parent		  = std::vector<int32>(lockCount, -1);

		for (int32 lockId = 0; lockId < lockCount; lockId++)
			Dfs(lockId);

		m_discoveredOrder.clear();
		m_finished.clear();
		m_parent.clear();
	}

	void DeadLockProfiler::Dfs(int32 here)
	{
		if (m_discoveredOrder[here] != -1)
			return;

		m_discoveredOrder[here] = m_discoveredCount++;

		auto findIt = m_lockHistory.find(here);
		if (findIt == m_lockHistory.end())
		{
			m_finished[here] = true;
			return;
		}

		std::set<int32>& nextSet = findIt->second;
		for (int32 there : nextSet)
		{
			if (m_discoveredOrder[there] == -1)
			{
				m_parent[there] = here;
				Dfs(there);
				continue;
			}

			if (m_discoveredOrder[here] < m_discoveredOrder[there])
				continue;

			if (m_finished[there] == false)
			{
				printf("%s -> %s\n", m_idToName[here], m_idToName[there]);

				int32 now = here;
				while (true)
				{
					printf("%s -> %s\n", m_idToName[m_parent[now]], m_idToName[now]);
					now = m_parent[now];
					if (now == there)
						break;
				}

				JAM_CRASH("DEADLOCK_DETECTED");
			}
		}

		m_finished[here] = true;
	}
}
