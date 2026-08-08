#pragma once

#include <jambase/SmallHash.h>

#include "jamnet/core/executor/RuntimeId.h"
#include "jamnet/runtime/world/data/WorldArchetypeDatabase.h"

#include <functional>


namespace jam::net
{
	struct WorldInstanceId
	{
		uint64 value = 0;

		constexpr bool IsValid() const noexcept { return value != 0; }
		constexpr auto operator<=>(const WorldInstanceId&) const = default;
	};

	inline constexpr WorldInstanceId kInvalidWorldInstanceId = {};

	// Static instance definitions derive a stable opaque id from their authored name.
	// Dynamic instances must be allocated by the server lifecycle service instead.
	inline WorldInstanceId MakeStaticWorldInstanceId(std::string_view name) noexcept
	{
		return { fnv1a<uint64>(name) };
	}

	struct WorldInstanceIdHash
	{
		size_t operator()(WorldInstanceId id) const noexcept
		{
			return std::hash<uint64>{}(id.value);
		}
	};



	// Transient server routing handle. Its generation makes a destroyed world
	// distinguishable from a later world that reuses the same local slot.
	using WorldId = RuntimeId;

	inline constexpr WorldId kInvalidWorldId = kInvalidRuntimeId;

	inline WorldId MakeWorldId(uint16 shardIndex, uint16 localIndex, uint32 generation)
	{
		return MakeRuntimeId(shardIndex, localIndex, generation);
	}

	inline uint16 GetWorldShardIndex(WorldId worldId)
	{
		return GetRuntimeShardIndex(worldId);
	}

	inline uint16 GetWorldLocalIndex(WorldId worldId)
	{
		return GetRuntimeLocalIndex(worldId);
	}

	inline uint32 GetWorldGeneration(WorldId worldId)
	{
		return GetRuntimeGeneration(worldId);
	}

	// Stable logical identity of a world instance and the archetype that defines
	// its content/configuration. This never identifies a shard-local object.
	struct WorldInstanceRef
	{
		WorldInstanceId		instanceId = kInvalidWorldInstanceId;
		WorldArchetypeKey	archetypeKey = {};

		bool IsValid() const noexcept
		{
			return instanceId.IsValid() && IsValidAssetKey(archetypeKey);
		}

		bool operator==(const WorldInstanceRef&) const = default;
	};

	struct WorldInstanceRefHash
	{
		size_t operator()(const WorldInstanceRef& ref) const noexcept
		{
			return SmallHashOf(ref.instanceId.value, ref.archetypeKey.v);
		}
	};

	// A live world is an incarnation of exactly one logical instance. The
	// archetype stays on WorldInstanceRef so routing does not become a content
	// lookup key again.
	struct WorldRef
	{
		WorldInstanceRef instance = {};
		WorldId			 worldId  = kInvalidWorldId;

		bool IsValid() const noexcept
		{
			return instance.IsValid() && worldId != kInvalidWorldId;
		}

		bool operator==(const WorldRef&) const = default;
	};

	struct WorldRefHash
	{
		size_t operator()(const WorldRef& ref) const noexcept
		{
			return SmallHashOf(ref.instance.instanceId.value, ref.worldId);
		}
	};

}
