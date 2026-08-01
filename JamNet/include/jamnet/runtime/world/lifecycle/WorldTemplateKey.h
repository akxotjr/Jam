#pragma once

#include <jambase/JamAsset.h>

#include <string_view>

namespace jam::net
{
	struct WorldTemplateKeyTag;
	using WorldTemplateKey = AssetKey<WorldTemplateKeyTag>;

	inline WorldTemplateKey MakeWorldTemplateKey(std::string_view name) noexcept
	{
		return MakeAssetKey<WorldTemplateKeyTag>(name);
	}
}
