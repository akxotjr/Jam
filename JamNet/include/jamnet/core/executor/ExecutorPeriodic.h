#pragma once

namespace jam
{
	struct PeriodicHandle
	{
		uint32 id = 0;
	};

	struct PeriodicOptions
	{
		Duration	period			= 0_ns;
		Duration	initialDelay	= 0_ns;
		int32		maxCatchUp		= 0;
		const char* name			= "Periodic";
	};
}
