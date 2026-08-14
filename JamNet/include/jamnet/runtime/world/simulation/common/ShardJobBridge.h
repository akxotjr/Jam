#pragma once
#include <jampx/IPhysicsJobBridge.h>

namespace jam
{
	class ShardExecutor;
}

namespace jam::net
{

	class ShardJobBridge final : public px::IPhysicsJobBridge
	{
	public:
		explicit ShardJobBridge(ShardExecutor& executor);

		void			SubmitJob(std::function<void()> fn) override;
		void			NotifyComplete(uint64_t awaitKey) override;
		bool			IsInFiberContext() const override;

	private:
		ShardExecutor&	m_executor;
	};


}
