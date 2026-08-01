#pragma once

#include "jamnet/runtime/world/lifecycle/WorldTemplateKey.h"
#include "jamnet/runtime/world/lifecycle/WorldActionTypes.h"

#include <unordered_map>


namespace jam::net
{
	struct WorldTemplateDatabase
	{
		int32 version = 1;
		std::unordered_map<WorldTemplateKey, WorldTemplateData> templates;

		const WorldTemplateData*	Find(WorldTemplateKey templateKey) const;
	};

} // namespace jam::net
