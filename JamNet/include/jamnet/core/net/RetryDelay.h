#pragma once

#include <jambase/JamTypes.h>
#include <jambase/SmallHash.h>

#include "jamnet/core/utils/TimeUnits.h"

#include <algorithm>


namespace jam::net
{
	inline Duration MakeRetryDelay(Duration baseDelay, Duration jitter, uint64 identity, uint32 token)
	{
		if (jitter.IsZero())
			return baseDelay;

		const Duration boundedJitter = std::min(baseDelay, jitter);
		const Duration window = boundedJitter * 2 + Duration::FromNs(1);
		return baseDelay - boundedJitter + Duration::FromNs(SmallHashOf(identity, token) % window.Ns());
	}
}
