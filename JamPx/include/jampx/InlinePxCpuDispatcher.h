#pragma once

#include "jampx/PhysXIncludes.h"
#include "jampx/PhysXTypes.h"

namespace jam::px
{
	class InlinePxCpuDispatcher final : public PxCpuDispatcher
	{
	public:
		void submitTask(PxBaseTask& task) override;
		PxU32 getWorkerCount() const override { return 1; }
	};
}
