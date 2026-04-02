#pragma once

#include "jamnet/core/utils/TaggedKey.h"


namespace jam
{
	struct ShardDomainKeyTag {};
	using ShardDomainKey = Key<ShardDomainKeyTag>;

	constexpr ShardDomainKey DomainOf(std::string_view name)
	{
		return KeyOf<ShardDomainKey>(name);
	}

	inline constexpr ShardDomainKey DOMAIN_NETWORK = DomainOf("NETWORK");
	inline constexpr ShardDomainKey DOMAIN_PHYSICS = DomainOf("PHYSICS");

	struct ShardDomain
	{
		ShardDomainKey	domain{};
		uint32			subType = 0;

		constexpr auto operator<=>(const ShardDomain&) const = default;
	};
} // namespace jam


namespace std
{
	template<>
	struct hash<jam::ShardDomainKey>
	{
		size_t operator()(const jam::ShardDomainKey& k) const noexcept
		{
			return std::hash<uint64>{}(k.v);
		}
	};

	template<>
	struct hash<jam::ShardDomain>
	{
		size_t operator()(const jam::ShardDomain& t) const noexcept
		{
			const size_t h0 = std::hash<uint64>{}(t.domain.v);
			const size_t h1 = std::hash<uint32>{}(t.subType);
			return h0 ^ (h1 + 0x9e3779b97f4a7c15ULL + (h0 << 6) + (h0 >> 2));
		}
	};
} // namespace std