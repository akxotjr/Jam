#pragma once
#include "Job.h"


namespace jam
{
	class IExecutor
	{
	public:
		virtual ~IExecutor() = default;
		virtual void Submit(Job j) = 0;
	};
}

