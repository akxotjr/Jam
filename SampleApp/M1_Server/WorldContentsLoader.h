#pragma once

#include "WorldContentsDatabase.h"

#include <string>

namespace jam::shared::gen
{
	struct WorldContentsRootDto;
}

namespace m1
{
	struct WorldContentsDatabaseBuilder
	{
		static WorldContentsDatabase Build(const jam::shared::gen::WorldContentsRootDto& dto);
	};

	struct WorldContentsLoader
	{
		static jam::shared::gen::WorldContentsRootDto LoadDto(const std::string& path);
		static WorldContentsDatabase Load(const std::string& path);
	};
}
