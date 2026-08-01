#pragma once

#include "jamnet/runtime/world/data/WorldTemplateDatabase.h"

#include <string>

namespace jam::shared::gen
{
	struct WorldTemplatesRootDto;
}

namespace jam::net
{
	struct WorldTemplateDatabaseBuilder
	{
		static WorldTemplateDatabase Build(const jam::shared::gen::WorldTemplatesRootDto& dto);
	};

	struct WorldTemplatesLoader
	{
		static jam::shared::gen::WorldTemplatesRootDto LoadDto(const std::string& path);
		static WorldTemplateDatabase Load(const std::string& path);
	};
}
