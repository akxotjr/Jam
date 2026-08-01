#pragma once

#include "jampx/IPhysicsJobBridge.h"
#include "jampx/PhysXIncludes.h"
#include "jampx/PhysXTypes.h"

namespace jam::px
{
	class ShardPxCpuDispatcher final : public PxCpuDispatcher
	{

	public:
		explicit ShardPxCpuDispatcher(IPhysicsJobBridge& bridge);

		void				submitTask(PxBaseTask& task) override;
		PxU32				getWorkerCount() const override { return 1; }

	private:
		IPhysicsJobBridge&	m_bridge;
	};
}
