#pragma once

#include "jamnet/runtime/actor/ActorArchetypeDatabase.h"

#include <nlohmann/json_fwd.hpp>

#include <string>

namespace jam::shared::gen
{
	struct ActorArchetypesRootDto;
}

namespace jam::net
{
	struct ActorArchetypeDatabaseBuilder
	{
		static ActorArchetypeDatabase Build(const jam::shared::gen::ActorArchetypesRootDto& dto);
	};

	struct ActorArchetypesLoader
	{
		using json = nlohmann::json;

		static json LoadJson(const std::string& path);
		static jam::shared::gen::ActorArchetypesRootDto LoadDto(const std::string& path);
		static jam::shared::gen::ActorArchetypesRootDto LoadDtoFromJson(const json& json);
		static ActorArchetypeDatabase Load(const std::string& path);
	};
}
