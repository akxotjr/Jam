#pragma once

#include "jamnet/runtime/world/data/WorldArchetypeDatabase.h"

#include <nlohmann/json_fwd.hpp>
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
		using json = nlohmann::json;

		static json LoadJson(const std::string& path);
		static jam::shared::gen::WorldArchetypesRootDto LoadDto(const std::string& path);
		static jam::shared::gen::WorldArchetypesRootDto LoadDtoFromJson(const json& json);
		static WorldArchetypeDatabase Load(const std::string& path);
	};
}
