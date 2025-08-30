#pragma once
#include "Clock.h"


namespace jam::utils::exec
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
	// todo
	inline RouteSeed RandomSeed()
	{
		const uint64 now = Clock::Instance().NowNs();
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
	struct GroupTag {};
	using RouteKey = StrongKey<RouteTag>;
	using GroupHomeKey = StrongKey<GroupTag>;


	class RoutingPolicy
	{
	public:
		explicit RoutingPolicy(RouteSeed s) : m_seed(s) {}

		// PerUser 기본
		RouteKey KeyForSession(uint64 user_id) const { return RouteKey(Mix64(user_id ^ m_seed.k0)); }

		// 그룹성 작업 오버라이드에 사용
		GroupHomeKey KeyForGroup(uint64 group_id) const { return GroupHomeKey(Mix64(group_id ^ m_seed.k1)); }

	private:
		RouteSeed m_seed;
	};
}

namespace std
{
	template<> struct hash<jam::utils::exec::RouteKey> {
		size_t operator()(jam::utils::exec::RouteKey k) const noexcept { return std::hash<uint64>{}(k.value()); }
	};
	template<> struct hash<jam::utils::exec::GroupHomeKey> {
		size_t operator()(jam::utils::exec::GroupHomeKey k) const noexcept { return std::hash<uint64>{}(k.value()); }
	};
}
