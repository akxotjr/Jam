#include "pch.h"

#include "jamnet/runtime/world/data/WorldTemplateDatabase.h"

namespace jam::net
{
	const WorldTemplateData* WorldTemplateDatabase::Find(WorldTemplateKey templateKey) const
	{
		auto it = templates.find(templateKey);
		return (it != templates.end()) ? &it->second : nullptr;
	}
}
