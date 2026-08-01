#include "pch.h"

#include "jamnet/runtime/world/data/ActorLevelDatabase.h"

namespace jam::net
{
	const ActorLevelInstanceData* ActorLevelDatabase::Find(uint32 actorId) const
	{
		for (const auto& instance : instances)
		{
			if (instance.actorId == actorId)
				return &instance;
		}

		return nullptr;
	}
}
