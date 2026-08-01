#pragma once

#include "jamnet/runtime/world/data/WorldInstanceDatabase.h"

#include <nlohmann/json_fwd.hpp>
#include <string>

namespace jam::net
{
	struct WorldInstancesLoader
	{
		using json = nlohmann::json;

		static json LoadJson(const std::string& path);
		static WorldInstanceDatabase Load(const std::string& path);
	};
}
