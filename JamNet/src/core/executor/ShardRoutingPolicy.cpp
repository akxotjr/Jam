#include "pch.h"
#include "jamnet/core/executor/ShardRoutingPolicy.h"

#include <limits>

namespace jam
{
	uint64 HashRouteDomain(std::string_view domain)
	{
		return HashTagKey(domain);
	}

	RouteKey ShardRoutingPolicy::MakeKey(RouteDomain domain, uint64 id) const
	{
		uint64 key = HashTagKey(domain.value(), m_seed.k0);
		key = HashTagKey(id, key ^ m_seed.k1);

		if (key == 0)
			key = 1;

		return RouteKey(key);
	}

	uint32 ShardRoutingPolicy::PickStableShard(RouteKey key, uint32 shardCount) const
	{
		if (shardCount == 0)
			return 0;

		uint32 bestShard = 0;
		uint64 bestScore = RendezvousScore(key, 0);
		for (uint32 shard = 1; shard < shardCount; ++shard)
		{
			const uint64 score = RendezvousScore(key, shard);
			if (score > bestScore)
			{
				bestShard = shard;
				bestScore = score;
			}
		}
		return bestShard;
	}

	uint32 ShardRoutingPolicy::PickPlacementCandidate(RouteKey key, uint32 candidateIndex, uint32 shardCount) const
	{
		if (shardCount == 0)
			return 0;

		uint64 score = HashTagKey(key.value(), m_seed.k0);
		score = HashTagKey(candidateIndex + 1, score);
		return static_cast<uint32>(score % shardCount);
	}

	uint64 ShardRoutingPolicy::RendezvousScore(RouteKey key, uint32 shardIndex) const
	{
		uint64 score = HashTagKey(key.value(), m_seed.k1);
		return HashTagKey(shardIndex, score);
	}

	uint64 ShardRoutingPolicy::PlacementScore(RouteKey key, uint32 shardIndex) const
	{
		uint64 score = HashTagKey(key.value(), m_seed.k0);
		return HashTagKey(shardIndex, score);
	}

	uint64 ShardRoutingPolicy::PlacementTieBreak(RouteKey key, uint32 shardIndex) const
	{
		uint64 score = HashTagKey(key.value(), m_seed.k0 ^ 0xbf58476d1ce4e5b9ULL);
		return HashTagKey(shardIndex, score);
	}

	RouteAssignment ShardRoutingPolicy::PlaceRoute(RouteKey key, std::span<const RouteLoadInfo> loads, const RoutePlacementOptions& opt) const
	{
		if (!IsValidRouteKey(key) || loads.empty())
			return {};

		switch (opt.policy)
		{
		case eRoutePolicy::StableSpread:
			return PlaceStableRoute(key, loads, opt.affinity);
		case eRoutePolicy::Placement:
			return PlaceLoadAwareRoute(key, loads, opt);
		default:
			return {};
		}
	}

	uint32 ShardRoutingPolicy::ResolveAffinityShard(const RouteAffinityHint& affinity, std::span<const RouteLoadInfo> loads) const
	{
		auto containsShard = [loads](uint32 shardIndex)
		{
			for (const RouteLoadInfo& load : loads)
				if (load.shardIndex == shardIndex)
					return true;
			return false;
		};

		if (affinity.preferredShard != kInvalidRouteShard && containsShard(affinity.preferredShard))
			return affinity.preferredShard;

		if (IsValidRouteKey(affinity.parentKey))
		{
			const uint32 parentIndex = PickStableShard(affinity.parentKey, static_cast<uint32>(loads.size()));
			if (parentIndex < loads.size())
				return loads[parentIndex].shardIndex;
		}

		return kInvalidRouteShard;
	}

	RouteAssignment ShardRoutingPolicy::PlaceStableRoute(RouteKey key, std::span<const RouteLoadInfo> loads, const RouteAffinityHint& affinity) const
	{
		const uint32 affinityShard = ResolveAffinityShard(affinity, loads);
		if (affinityShard != kInvalidRouteShard)
			return RouteAssignment{ .key = key, .shardIndex = affinityShard, .valid = true };

		const uint32 shardIndex = PickStableShard(key, static_cast<uint32>(loads.size()));
		if (shardIndex < loads.size())
			return RouteAssignment{ .key = key, .shardIndex = loads[shardIndex].shardIndex, .valid = true };

		return {};
	}

	RouteAssignment ShardRoutingPolicy::PlaceLoadAwareRoute(RouteKey key, std::span<const RouteLoadInfo> loads, const RoutePlacementOptions& opt) const
	{
		const uint32 candidateCount =
			std::clamp<uint32>(opt.topK, 1u, std::min<uint32>(kMaxPlacementCandidates, static_cast<uint32>(loads.size())));

		std::array<RouteCandidate, kMaxPlacementCandidates> top{};
		uint32 topCount = 0;

		const uint32 affinityShard = ResolveAffinityShard(opt.affinity, loads);
		if (affinityShard != kInvalidRouteShard)
		{
			if (opt.affinity.hard)
				return RouteAssignment{ .key = key, .shardIndex = affinityShard, .valid = true };

			TryInsertTopCandidate(
				top,
				topCount,
				candidateCount,
				RouteCandidate{ .shardIndex = affinityShard, .score = std::numeric_limits<uint64>::max() });
		}

		for (const RouteLoadInfo& load : loads)
		{
			if (load.shardIndex == affinityShard)
				continue;

			const uint64 score = PlacementScore(key, load.shardIndex);
			TryInsertTopCandidate(top, topCount, candidateCount, RouteCandidate{ .shardIndex = load.shardIndex, .score = score });
		}

		uint32 bestShard = top[0].shardIndex;
		uint64 bestLoad  = std::numeric_limits<uint64>::max();
		uint64 bestScore = top[0].score;

		for (uint32 i = 0; i < topCount; ++i)
		{
			const uint32 shard = top[i].shardIndex;
			const uint64 score = top[i].score;
			uint64 loadValue = std::numeric_limits<uint64>::max();

			for (const RouteLoadInfo& load : loads)
			{
				if (load.shardIndex == shard)
				{
					loadValue = load.load;
					break;
				}
			}

			if (loadValue < bestLoad || (loadValue == bestLoad && score > bestScore))
			{
				bestShard = shard;
				bestLoad  = loadValue;
				bestScore = score;
			}
		}

		return RouteAssignment{ .key = key, .shardIndex = bestShard, .valid = true };
	}

	void ShardRoutingPolicy::TryInsertTopCandidate(std::array<RouteCandidate, kMaxPlacementCandidates>& top, uint32& topCount, uint32 limit, RouteCandidate candidate)
	{
		if (limit == 0)
			return;

		if (topCount < limit)
		{
			top[topCount++] = candidate;
		}
		else
		{
			uint32 worstIndex = 0;
			for (uint32 i = 1; i < topCount; ++i)
			{
				if (top[i].score < top[worstIndex].score)
					worstIndex = i;
			}

			if (candidate.score <= top[worstIndex].score)
				return;

			top[worstIndex] = candidate;
		}

		for (uint32 i = 1; i < topCount; ++i)
		{
			RouteCandidate v = top[i];
			uint32 j = i;
			while (j > 0 && top[j - 1].score < v.score)
			{
				top[j] = top[j - 1];
				--j;
			}
			top[j] = v;
		}
	}
}
