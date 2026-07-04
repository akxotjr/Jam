#pragma once

#include "jampx/PhysicsDatabase.h"

#include <nlohmann/json_fwd.hpp>

#include <string>

namespace jam::shared::gen
{
	struct PhysicsAssetRootDto;
}

namespace jam::px
{
	struct PhysicsAssetBuilder
	{
		static PhysicsDatabase Build(const jam::shared::gen::PhysicsAssetRootDto& dto);
	};

	struct PhysicsAssetLoader
	{
		using json = nlohmann::json;

		static json LoadJson(const std::string& path);
		static jam::shared::gen::PhysicsAssetRootDto LoadDto(const std::string& path);
		static jam::shared::gen::PhysicsAssetRootDto LoadDtoFromJson(const json& json);
		static PhysicsDatabase Load(const std::string& path);
	};
}
