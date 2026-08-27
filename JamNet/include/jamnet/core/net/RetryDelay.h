#pragma once

#include <jambase/JamTypes.h>
#include <jambase/SmallHash.h>

#include <algorithm>


namespace jam::net
{
	inline uint64 MakeRetryDelayNs(uint64 baseDelayNs, uint64 jitterNs, uint64 identity, uint32 token)
	{
		if (jitterNs == 0)
			return baseDelayNs;

		const uint64 boundedJitterNs = std::min(baseDelayNs, jitterNs);
		const uint64 windowNs = boundedJitterNs * 2 + 1;
		return baseDelayNs - boundedJitterNs + SmallHashOf(identity, token) % windowNs;
	}
}
