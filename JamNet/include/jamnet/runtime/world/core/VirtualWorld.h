#pragma once

#include "jamnet/runtime/world/core/WorldBase.h"

namespace jam::net
{
	class VirtualWorld : public WorldMembershipHost
	{
	public:
		VirtualWorld(const WorldConfig& config);
		~VirtualWorld() override = default;
	};
}
