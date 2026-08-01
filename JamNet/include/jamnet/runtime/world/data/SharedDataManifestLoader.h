#pragma once

#include "jamnet/runtime/world/data/SharedDataManifest.h"

#include <string>


namespace jam::shared::gen
{
	struct SharedDataManifestRootDto;
}

namespace jam::net
{
	struct SharedDataManifestBuilder
	{
		static SharedDataManifest Build(const jam::shared::gen::SharedDataManifestRootDto& dto);
	};

	struct SharedDataManifestLoader
	{
		static jam::shared::gen::SharedDataManifestRootDto LoadDto(const std::string& path);
		static SharedDataManifest Load(const std::string& path);
	};
}
