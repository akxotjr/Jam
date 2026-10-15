#include "pch.h"
#include "jampx/InlinePxCpuDispatcher.h"

namespace jam::px
{
	void InlinePxCpuDispatcher::submitTask(PxBaseTask& task)
	{
		task.run();
		task.release();
	}
}
