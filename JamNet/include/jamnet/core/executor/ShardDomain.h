#pragma once

#include <jambase/TaggedKey.h>


namespace jam
{
	struct ShardDomainKeyTag {};
	using ShardDomainKey = TagKey<ShardDomainKeyTag>;

	inline ShardDomainKey DomainOf(std::string_view name)
	{
		return TagKeyOf<ShardDomainKey>(name);
	}

	inline const ShardDomainKey DOMAIN_NETWORK = DomainOf("NETWORK");
	inline const ShardDomainKey DOMAIN_PHYSICS = DomainOf("PHYSICS");

	struct ShardDomain
	{
		ShardDomainKey	domain  = {};
		uint32			subType = 0;

		constexpr auto operator<=>(const ShardDomain&) const = default;
	};
} // namespace jam


namespace std
{
	template<>
	struct hash<jam::ShardDomain>
	{
		size_t operator()(const jam::ShardDomain& t) const noexcept
		{
			return static_cast<size_t>(jam::HashTagKey(t.subType, t.domain.value()));
		}
	};
} // namespace std
