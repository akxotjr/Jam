#pragma once
#include <string_view>

#include "JamTypes.h"
#include "Fnv1a.h"


namespace jam
{
	template<class Tag>
	using AssetKey = Fnv1aHandle<Tag, uint64>;

	template<class Tag>
	[[nodiscard]] inline AssetKey<Tag> MakeAssetKey(std::string_view name) noexcept
	{
		return AssetKey<Tag>::FromU64(fnv1a<uint64>(name));
	}

	template<class KeyT>
	[[nodiscard]] inline bool IsValidAssetKey(KeyT key) noexcept
	{
		return key.v != 0;
	}

} // namespace jam
