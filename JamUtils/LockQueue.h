#pragma once
#include "Lock.h"

namespace jam::utils::thread
{
	template<typename T>
	class LockQueue
	{
	public:
		void Push(T job)
		{
			WRITE_LOCK
			m_items.push(job);
		}

		std::optional<T> TryPop()
		{
			WRITE_LOCK
			if (m_items.empty())
				return std::nullopt;

			T job = std::move(m_items.front());
			m_items.pop();
			return job;
		}

		T Pop()
		{
			WRITE_LOCK
				if (m_items.empty())
					return T();

			T ret = m_items.front();
			m_items.pop();
			return ret;
		}

		void PopAll(OUT xvector<T>& items)
		{
			WRITE_LOCK
			while (T item = Pop())
			{
				items.push_back(item);
			}
		}

		void Clear()
		{
			WRITE_LOCK
			m_items = xqueue<T>();
		}

	private:
		USE_LOCK
		xqueue<T>	m_items;
	};
}
