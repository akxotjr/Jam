#pragma once

#include "WorldContentsDatabase.h"

#include <string>

namespace jam::shared::gen
{
	struct M1WorldContentsRootDto;
}

namespace m1
{
	struct WorldContentsDatabaseBuilder
	{
		static WorldContentsDatabase Build(const jam::shared::gen::M1WorldContentsRootDto& dto);
	};

	struct WorldContentsLoader
	{
		static jam::shared::gen::M1WorldContentsRootDto LoadDto(const std::string& path);
		static WorldContentsDatabase Load(const std::string& path);
	};
}
