#pragma once

#include <jambase/TaggedKey.h>

#include <array>
#include <functional>
#include <limits>
#include <span>
#include <string_view>
#include <vector>


namespace jam
{
	struct RouteSeed
	{
		uint64 k0 = 0x9e3779b97f4a7c15ULL;
		uint64 k1 = 0xbf58476d1ce4e5b9ULL;
	};

	uint64 HashRouteDomain(std::string_view domain);

	struct RouteDomainTag {};
	struct RouteTag {};

	struct RouteDomain : TagKey<RouteDomainTag>
	{
		using Base = TagKey<RouteDomainTag>;
		using Base::Base;

		constexpr RouteDomain() = default;
		static RouteDomain From(std::string_view domain) { return RouteDomain(HashRouteDomain(domain)); }
	};

	using RouteKey = TagKey<RouteTag>;

	inline bool IsValidRouteKey(RouteKey key)
	{
		return key.value() != 0;
	}

	enum class eRoutePolicy : uint8
	{
		StableSpread,
		Placement,
	};

	inline constexpr uint32 kInvalidRouteShard = std::numeric_limits<uint32>::max();

	struct RouteAffinityHint
	{
		RouteKey	parentKey		= {};
		uint32		preferredShard	= kInvalidRouteShard;
		bool		hard			= false;
	};

	struct RoutePlacementOptions
	{
		eRoutePolicy		policy		= eRoutePolicy::Placement;
		uint32				topK		= 2;
		RouteAffinityHint	affinity	= {};
	};

	struct RouteAssignment
	{
		RouteKey	key			= {};
		uint32		shardIndex	= 0;
		bool		valid		= false;
	};

	struct RouteCandidate
	{
		uint32		shardIndex	= 0;
		uint64		score		= 0;
	};

	struct RouteLoadInfo
	{
		uint32		shardIndex	= 0;
		uint64		load		= 0;
	};

	inline bool IsValidRouteAssignment(const RouteAssignment& assignment)
	{
		return assignment.valid && IsValidRouteKey(assignment.key);
	}

	class ShardRoutingPolicy
	{
	public:
		ShardRoutingPolicy() = default;
		explicit	ShardRoutingPolicy(RouteSeed seed) : m_seed(seed) {}

		RouteKey			MakeKey(RouteDomain domain, uint64 id) const;
		RouteKey			MakeKey(std::string_view domain, uint64 id) const { return MakeKey(RouteDomain::From(domain), id); }

		uint32				PickStableShard(RouteKey key, uint32 shardCount) const;
		uint32				PickPlacementCandidate(RouteKey key, uint32 candidateIndex, uint32 shardCount) const;

		RouteAssignment		PlaceRoute(RouteKey key, std::span<const RouteLoadInfo> loads, const RoutePlacementOptions& opt) const;

		static constexpr uint32 kMaxPlacementCandidates = 8;

	private:
		uint32				ResolveAffinityShard(const RouteAffinityHint& affinity, std::span<const RouteLoadInfo> loads) const;
		RouteAssignment		PlaceStableRoute(RouteKey key, std::span<const RouteLoadInfo> loads, const RouteAffinityHint& affinity) const;
		RouteAssignment		PlaceLoadAwareRoute(RouteKey key, std::span<const RouteLoadInfo> loads, const RoutePlacementOptions& opt) const;
		uint64				RendezvousScore(RouteKey key, uint32 shardIndex) const;
		uint64				PlacementScore(RouteKey key, uint32 shardIndex) const;
		uint64				PlacementTieBreak(RouteKey key, uint32 shardIndex) const;

		static void			TryInsertTopCandidate(
			std::array<RouteCandidate, kMaxPlacementCandidates>& top,
			uint32& topCount,
			uint32 limit,
			RouteCandidate candidate);


		RouteSeed	m_seed;
	};
}

namespace std
{
	template<> struct hash<jam::RouteDomain>
	{
		size_t operator()(jam::RouteDomain domain) const noexcept { return std::hash<uint64>{}(domain.value()); }
	};
}
