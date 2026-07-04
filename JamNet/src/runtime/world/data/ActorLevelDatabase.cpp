#include "pch.h"

#include "jamnet/runtime/world/data/ActorLevelDatabase.h"

namespace jam::net
{
	const ActorLevelInstanceData* ActorLevelDatabase::Find(uint32 levelActorId) const
	{
		for (const auto& instance : instances)
		{
			if (instance.levelActorId == levelActorId)
				return &instance;
		}

		return nullptr;
	}
}
