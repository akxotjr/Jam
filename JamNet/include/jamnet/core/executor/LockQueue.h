#pragma once
#include "jamnet/core/executor/Lock.h"

namespace jam
{
	template<typename T>
	class LockQueue
	{
	public:
		void Push(T job)
		{
			WRITE_LOCK
			m_items.push(std::move(job));
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

			T ret = std::move(m_items.front());
			m_items.pop();
			return ret;
		}

		void PopAll(OUT std::vector<T>& items)
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
			m_items = std::queue<T>();
		}

	private:
		USE_LOCK
		std::queue<T>	m_items;
	};
}
