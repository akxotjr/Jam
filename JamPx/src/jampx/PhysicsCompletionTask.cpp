#include "pch.h"
#include "jampx/PhysicsCompletionTask.h"


namespace jam::px
{
	void PhysicsCompletionTask::run()
	{
		if (m_bridge && m_awaitKey != 0)
			m_bridge->NotifyComplete(m_awaitKey);
	}
}
