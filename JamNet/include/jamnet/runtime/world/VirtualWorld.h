#pragma once

#include "jamnet/runtime/world/WorldBase.h"

namespace jam::net
{
	class VirtualWorld : public WorldMembershipHost
	{
	public:
		VirtualWorld(const WorldConfig& config);
		~VirtualWorld() override = default;
	};
}
