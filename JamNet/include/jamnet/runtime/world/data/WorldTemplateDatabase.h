#pragma once

#include "jamnet/runtime/world/types/WorldTemplateKey.h"
#include <unordered_map>

#include "jamnet/runtime/world/types/WorldActionTypes.h"

namespace jam::net
{
	struct WorldTemplateDatabase
	{
		int32 version = 1;
		std::unordered_map<WorldTemplateKey, WorldTemplateData> templates;

		const WorldTemplateData*	Find(WorldTemplateKey templateKey) const;
	};

} // namespace jam::net
