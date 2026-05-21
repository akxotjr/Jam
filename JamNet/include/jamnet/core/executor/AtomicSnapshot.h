#pragma once

#include <memory>
#include <atomic>

namespace jam
{

	template<typename T>
	class AtomicSnapshot
	{
	public:
		using Ptr = std::shared_ptr<const T>;

		AtomicSnapshot()
		{
			StoreUnlocked(std::make_shared<T>());
		}

		explicit AtomicSnapshot(Ptr initial)
		{
			StoreUnlocked(std::move(initial));
		}

		Ptr Load() const noexcept
		{
			return m_snapshot.load(std::memory_order_acquire);
		}

		template<typename Fn>
		void Update(Fn&& fn)
		{
			std::lock_guard lock(m_writeLock);

			auto old = Load();
			auto next = std::make_shared<T>(*old);

			fn(*next);

			StoreUnlocked(std::move(next));
		}

		void Replace(Ptr next)
		{
			std::lock_guard lock(m_writeLock);
			StoreUnlocked(std::move(next));
		}

	private:
		void StoreUnlocked(Ptr next) noexcept
		{
			m_snapshot.store(std::move(next), std::memory_order_release);
		}

	private:
		std::atomic<Ptr> m_snapshot;
		std::mutex		 m_writeLock;
	};
}
