#pragma once
#include "jamnet/core/executor/IExecutor.h"

namespace jam
{
	class MainExecutor : public IExecutor
	{
		DECLARE_SINGLETON_INHERITANCE(MainExecutor)

	public:
		void			Init();
		bool			IsMainThread() const noexcept { return std::this_thread::get_id() == m_id; }

		void			Submit(Job j) override;
		size_t			PumpOnce(size_t maxCnt = 1024);
		void			DrainAll();

	private:
		std::thread::id						m_id		= {};
		moodycamel::ConcurrentQueue<Job>	m_queue;
		std::atomic<size_t>					m_pending	= 0;
	};
}


#define MAIN_EXEC			jam::MainExecutor::Instance()
#define MAIN_EXEC_INIT()    jam::MainExecutor::Instance().Init()