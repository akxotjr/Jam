#include "pch.h"
#include "jamnet/runtime/world/VirtualWorld.h"

namespace jam::net
{
	VirtualWorld::VirtualWorld(const WorldConfig& config)
		: WorldMembershipHost(config)
	{
	}
} 
