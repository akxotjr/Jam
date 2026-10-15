#pragma once

#include <entt/entity/sparse_set.hpp>
#include <vector>
#include "jamnet/runtime/world/actor/ActorId.h"

namespace jam::net
{
	struct UserAoiState
	{
		// Current AOI visibility (range/LOS), independent of the client's replication state.
		entt::sparse_set			visible;

		// This tick spans the previous replication pass's completion through the current pass.
		// Actors with at least one invisible-to-visible transition in that interval, deduplicated.
		// Retained on leave/destruction; not necessarily a subset of visible. No transition order/count.
		entt::sparse_set			entered;

		// ActorIds for visible-to-invisible transitions or destruction while visible in the same interval.
		// Retained on reentry; may overlap entered and contain duplicates. IDs survive entity destruction.
		// Replication resolves the final lifecycle using current visibility and client knowledge;
		// entered/left are cleared after the replication pass, not after successful packet delivery.
		std::vector<ActorId>		left;
	};
}
