#pragma once

#include <jambase/JamTypes.h>

namespace jam::net
{
	struct WorldSyncToken
	{
		uint64 value = 0;
		bool IsValid() const noexcept { return value != 0; }
		auto operator<=>(const WorldSyncToken&) const = default;
	};

	enum class eWorldSyncKind : uint8
	{
		WorldPrepare = 0,
		WorldResync,
		ReplicationBaseline,
		WorldContent,
	};
}
