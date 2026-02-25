#pragma once

#include "jampx/api/IPhysicsJobBridge.h"
#include <physx/task/PxTask.h>

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