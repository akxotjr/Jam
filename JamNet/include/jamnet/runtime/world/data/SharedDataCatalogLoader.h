#pragma once

#include "jamnet/runtime/world/data/SharedDataCatalog.h"

#include <nlohmann/json_fwd.hpp>
#include <string>

namespace jam::shared::gen
{
	struct SharedDataCatalogRootDto;
}

namespace jam::net
{
	struct SharedDataCatalogBuilder
	{
		static SharedDataCatalog Build(const jam::shared::gen::SharedDataCatalogRootDto& dto);
	};

	struct SharedDataCatalogLoader
	{
		using json = nlohmann::json;

		static json LoadJson(const std::string& path);
		static jam::shared::gen::SharedDataCatalogRootDto LoadDto(const std::string& path);
		static jam::shared::gen::SharedDataCatalogRootDto LoadDtoFromJson(const json& json);
		static SharedDataCatalog Load(const std::string& path);
	};
}
