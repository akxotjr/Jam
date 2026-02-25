#pragma once
#include <string_view>

namespace jam
{
	struct RouteSeed
	{
		uint64 k0;
		uint64 k1;
	};

	inline uint64 Mix64(uint64 x)
	{
		x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
		x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL;
		x ^= x >> 33; return x;
	}

	// based on current time
	inline RouteSeed RandomSeed()
	{
		const uint64 now = NOW_NS();
		return { .k0 = now ^ 0x9e3779b97f4a7c15ULL, .k1= ~now };
	}


	template <class Tag>
	struct StrongKey
	{
		uint64 v = 0;
		constexpr StrongKey() = default;
		constexpr explicit StrongKey(uint64 x) : v(x) {}
		constexpr uint64 value() const { return v; }
		friend constexpr bool operator==(StrongKey a, StrongKey b) { return a.v == b.v; }
	};
	struct RouteTag {};
	using RouteKey = StrongKey<RouteTag>;

	static bool IsValidRouteKey(RouteKey key)
	{
		return key.value() != 0;
	}


	class RoutingPolicy
	{
	public:
		explicit RoutingPolicy(RouteSeed s) : m_seed(s) {}

		RouteKey KeyForAffinity(std::string_view domain, uint64 id) const
		{
			uint64 hash = 1469598103934665603ULL;
			for (unsigned char c : domain) 
			{
				hash ^= c;
				hash *= 1099511628211ULL;
			}
			return RouteKey(Mix64(Mix64(hash ^ id) ^ m_seed.k0));
		}

	private:
		RouteSeed		m_seed;
	};
}

namespace std
{
	template<> struct hash<jam::RouteKey> {
		size_t operator()(jam::RouteKey k) const noexcept { return std::hash<uint64>{}(k.value()); }
	};
}
