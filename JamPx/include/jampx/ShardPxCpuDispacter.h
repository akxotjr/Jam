#pragma once

#include "jampx/IPhysicsJobBridge.h"

namespace jam::px
{
	class ShardPxCpuDispacter final : public PxCpuDispatcher
	{

	public:
		explicit ShardPxCpuDispacter(IPhysicsJobBridge& bridge);

		void				submitTask(PxBaseTask& task) override;
		PxU32				getWorkerCount() const override { return 1; }

	private:
		IPhysicsJobBridge&	m_bridge;
	};
}