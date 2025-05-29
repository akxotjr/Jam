#pragma once

namespace jam::utils::thrd
{
	template<typename T>
	class LockDeque
	{
	public:
		void PushFront(const T& job)
		{
			WRITE_LOCK
			m_items.push_front(job);
		}

		void PushBack(const T& job)
		{
			WRITE_LOCK
			m_items.push_back(job);
		}

		std::optional<T> TryPopFront()
		{
			WRITE_LOCK
			if (m_items.empty())
				return std::nullopt;

			T job = std::move(m_items.front());
			m_items.pop_front();
			return job;
		}

		std::optional<T> TryStealBack()
		{
			WRITE_LOCK
			if (m_items.empty())
				return std::nullopt;

			T job = std::move(m_items.back());
			m_items.pop_back();
			return job;
		}

		void PopAll(OUT xvector<T>& items)
		{
			WRITE_LOCK
			while (!m_items.empty())
			{
				items.push_back(m_items.front());
				m_items.pop_front();
			}
		}

		void Clear()
		{
			WRITE_LOCK
			m_items.clear();
		}

	private:
		USE_LOCK
		xdeque<T> m_items;
	};

}
