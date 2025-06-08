#pragma once

namespace jam::utils::thrd
{
	class Fiber;

	class FiberScheduler
	{
	public:
		FiberScheduler();
		~FiberScheduler();

        void AddFiber(Sptr<Fiber> fiber)
        {
            WRITE_LOCK
        	m_readyQueue.push(fiber);
        }

        std::optional<Sptr<Fiber>> NextFiber()
        {
            WRITE_LOCK
            if (m_readyQueue.empty())
                return std::nullopt;

            auto fiber = m_readyQueue.front();
            m_readyQueue.pop();
            return fiber;
        }

        bool Empty()
        {
            WRITE_LOCK
            return m_readyQueue.empty();
        }

	private:
        USE_LOCK

		xqueue<Sptr<Fiber>> m_readyQueue;
	};
}
