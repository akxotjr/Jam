#pragma once

#include "jamnet/runtime/world/types/WorldActionTypes.h"
#include <functional>

namespace jam::net
{
	class WorldBase;

	class IWorldActionSystem
	{
	public:
		virtual ~IWorldActionSystem() = default;

		virtual void	Shutdown() = 0;
		virtual void	Execute(WorldActionRequest req) = 0;

		virtual void	CreateWorld(INOUT WorldKey& key) = 0;
		virtual void	DestroyWorld(const WorldKey& key) = 0;

		virtual bool	SubmitWorldJob(const WorldKey& key, std::function<void(WorldBase&)> job) = 0;
	};
}
