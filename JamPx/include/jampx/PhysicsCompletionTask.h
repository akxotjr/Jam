#pragma once

#include "jampx/api/IPhysicsJobBridge.h"

#include <physx/task/PxTask.h>

namespace jam::px
{
	class PhysicsCompletionTask : public PxLightCpuTask
	{
	public:
		void				run() override;
		const char*			getName() const override { return "PhysicsCompletionTask"; }

		void				SetPhysicsJobBridge(IPhysicsJobBridge* bridge) { m_bridge = bridge; }
		void				SetAwaitKey(uint64 awaitKey) { m_awaitKey = awaitKey; }
		uint64				GetAwaitKey() const { return m_awaitKey; }

	private:
		IPhysicsJobBridge*	m_bridge = nullptr;
		uint64				m_awaitKey = 0;
	};
}
