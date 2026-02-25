#include "pch.h"
#include "jampx/ShardPxCpuDispacter.h"


namespace jam::px
{
	ShardPxCpuDispacter::ShardPxCpuDispacter(IPhysicsJobBridge& bridge)
		: m_bridge(bridge)
	{
	}


	void ShardPxCpuDispacter::submitTask(physx::PxBaseTask& task)
	{
		if (m_bridge.IsInFiberContext())
		{
			// Fiber path 
			// Async: Submit to shard job queue -> Loop processes after suspend
			PxBaseTask* t = &task;
			m_bridge.SubmitJob([t]()
				{
					t->run();
					t->release();
				});
		}
		else
		{
			// DomainSystem path
			// Inline synchronous execution: fetchResults completes before blocking
			task.run();
			task.release();
		}
	}

}