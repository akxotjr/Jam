#pragma once

#include "jamnet/runtime/world/data/ActorLevelDatabase.h"

#include <nlohmann/json_fwd.hpp>
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
		using json = nlohmann::json;

		static json LoadJson(const std::string& path);
		static jam::shared::gen::ActorLevelsRootDto LoadDto(const std::string& path);
		static jam::shared::gen::ActorLevelsRootDto LoadDtoFromJson(const json& json);
		static ActorLevelDatabase Load(const std::string& path);
	};
}
