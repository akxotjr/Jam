#pragma once
#include "ISingletonLayer.h"

namespace jam::utils::thread
{
	using Callback = std::function<void()>;

	class ThreadManager : public ISingletonLayer<ThreadManager>
	{
		friend class jam::ISingletonLayer<ThreadManager>;

	public:
		void							Init() override;
		void							Shutdown() override;

		void							Launch(Callback callback);
		void							Join();

		void							InitTLS();
		void							DestroyTLS();

		int32							DoGlobalQueueWork();
		void							DistributeReservedJob();

	private:
		Mutex							m_lock;
		std::vector<std::thread>		m_threads;

		
	};
}
