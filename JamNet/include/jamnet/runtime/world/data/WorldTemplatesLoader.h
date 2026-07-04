#pragma once

#include "jamnet/runtime/world/data/WorldTemplateDatabase.h"

#include <nlohmann/json_fwd.hpp>

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
		using json = nlohmann::json;

		static json LoadJson(const std::string& path);
		static jam::shared::gen::WorldTemplatesRootDto LoadDto(const std::string& path);
		static jam::shared::gen::WorldTemplatesRootDto LoadDtoFromJson(const json& json);
		static WorldTemplateDatabase Load(const std::string& path);
	};
}
