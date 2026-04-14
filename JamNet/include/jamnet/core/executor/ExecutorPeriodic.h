#pragma once

namespace jam
{
	struct PeriodicHandle
	{
		uint32 id = 0;
	};

	struct PeriodicOptions
	{
		uint64		period_ns		= 0_ns;
		uint64		initialDelay_ns = 0_ns;
		int32		maxCatchUp		= 0;
		const char* name			= "Periodic";
	};
}
