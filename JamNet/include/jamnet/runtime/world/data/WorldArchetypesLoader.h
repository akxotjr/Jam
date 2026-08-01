#pragma once

#include "jamnet/runtime/world/data/WorldArchetypeDatabase.h"

#include <string>

namespace jam::shared::gen
{
	struct WorldArchetypesRootDto;
}

namespace jam::net
{
	struct WorldArchetypeDatabaseBuilder
	{
		static WorldArchetypeDatabase Build(const jam::shared::gen::WorldArchetypesRootDto& dto);
	};

	struct WorldArchetypesLoader
	{
		static jam::shared::gen::WorldArchetypesRootDto LoadDto(const std::string& path);
		static WorldArchetypeDatabase Load(const std::string& path);
	};
}
