#pragma once

#include <cstdint>

namespace jam::net
{
	using ClientRequestId = uint32_t;
	inline constexpr ClientRequestId kInvalidClientRequestId = 0;
}
