#include "pch.h"

#include "jamnet/runtime/world/actor/ActorArchetypeDatabase.h"

namespace jam::net
{
	const ActorArchetypeData* ActorArchetypeDatabase::Find(ActorArchetypeKey key) const
	{
		if (const auto it = archetypes.find(key); it != archetypes.end())
			return &it->second;
		return nullptr;
	}
}
