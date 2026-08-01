#pragma once

#include "jamnet/runtime/world/data/ActorLevelDatabase.h"

#include <string>

namespace jam::shared::gen
{
	struct ActorLevelsRootDto;
}

namespace jam::net
{
	struct ActorLevelDatabaseBuilder
	{
		static ActorLevelDatabase Build(const jam::shared::gen::ActorLevelsRootDto& dto);
	};

	struct ActorLevelsLoader
	{
		static jam::shared::gen::ActorLevelsRootDto LoadDto(const std::string& path);
		static ActorLevelDatabase Load(const std::string& path);
	};
}
