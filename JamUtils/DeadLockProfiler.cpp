#include "pch.h"
#include "DeadLockProfiler.h"

namespace jam::utils::thrd
{
	/*--------------------
		DeadLockProfiler
	---------------------*/

	void DeadLockProfiler::PushLock(const char* name)
	{
		LockGuard guard(m_lock);

		// 아이디를 찾거나 발급한다.
		int32 lockId = 0;

		auto findIt = m_nameToId.find(name);
		if (findIt == m_nameToId.end())
		{
			lockId = static_cast<int32>(m_nameToId.size());
			m_nameToId[name] = lockId;
			m_idToName[lockId] = name;
		}
		else
		{
			lockId = findIt->second;
		}

		// 잡고 있는 락이 있었다면
		if (tl_LockStack.empty() == false)
		{
			// 기존에 발견되지 않은 케이스라면 데드락 여부 다시 확인한다.
			const int32 prevId = tl_LockStack.top();
			if (lockId != prevId)
			{
				xset<int32>& history = m_lockHistory[prevId];
				if (history.find(lockId) == history.end())
				{
					history.insert(lockId);
					CheckCycle();
				}
			}
		}

		tl_LockStack.push(lockId);
	}

	void DeadLockProfiler::PopLock(const char* name)
	{
		LockGuard guard(m_lock);

		if (tl_LockStack.empty())
			CRASH("MULTIPLE_UNLOCK");

		int32 lockId = m_nameToId[name];
		if (tl_LockStack.top() != lockId)
			CRASH("INVALID_UNLOCK");

		tl_LockStack.pop();
	}

	void DeadLockProfiler::CheckCycle()
	{
		const int32 lockCount = static_cast<int32>(m_nameToId.size());
		m_discoveredOrder = xvector<int32>(lockCount, -1);
		m_discoveredCount = 0;
		m_finished = xvector<bool>(lockCount, false);
		m_parent = xvector<int32>(lockCount, -1);

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

		xset<int32>& nextSet = findIt->second;
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

			// 순방향이 아니고, Dfs(there)가 아직 종료하지 않았다면, there는 here의 선조이다. (역방향 간선)
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

				CRASH("DEADLOCK_DETECTED")
			}
		}

		m_finished[here] = true;
	}
}
