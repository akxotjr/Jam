#pragma once

namespace jam::net
{
	using WorldId = uint32;
	inline constexpr WorldId INVALID_WORLD_ID = 0;

	enum class eWorldKind : uint8
	{
		Unknown			= 0,
		SharedWorld		= 1,
		FieldChannel	= 2,
		DungeonInstance	= 3,
		RoomInstance	= 4,
		PrivateWorld	= 5,
	};

	enum class eWorldAssignmentStatus : uint8
	{
		Assigned	= 0,
		Waiting		= 1,
		Failed		= 2,
	};

	enum class eWorldAssignmentAction : uint8
	{
		None		= 0,
		Join		= 1,
		Provision	= 2,
		Transfer	= 3,
		Reject		= 4,
	};

	struct WorldKey
	{
		eWorldKind	kind		= eWorldKind::Unknown;
		uint32		templateId	= 0;
		uint64		instanceId	= 0;

		static WorldKey FromSharedWorldTemplate(uint32 templateId)
		{
			return WorldKey
			{
				.kind		= (templateId != 0) ? eWorldKind::SharedWorld : eWorldKind::Unknown,
				.templateId = templateId,
				.instanceId	= 0,
			};
		}

		bool IsValid() const
		{
			return kind != eWorldKind::Unknown && (templateId != 0 || instanceId != 0);
		}

		bool operator==(const WorldKey&) const = default;
	};

	struct WorldKeyHash
	{
		size_t operator()(const WorldKey& key) const noexcept
		{
			const size_t h1 = std::hash<uint8>{}(static_cast<uint8>(key.kind));
			const size_t h2 = std::hash<uint32>{}(key.templateId);
			const size_t h3 = std::hash<uint64>{}(key.instanceId);

			size_t seed = h1;
			seed ^= h2 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
			seed ^= h3 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
			return seed;
		}
	};

	inline constexpr WorldKey INVALID_WORLD_KEY{};

	struct WorldOptions
	{
		bool	persistent			= true;
		bool	destroyWhenEmpty	= false;
		bool	isPrivate			= false;
		uint32	capacity			= 0;
		uint32	shardHint			= 0;
	};

	struct WorldAssignmentRequest
	{
		uint64		principalId		= 0;
		WorldId		currentWorldId	= INVALID_WORLD_ID;
		WorldKey	currentWorld	= INVALID_WORLD_KEY;
	};

	struct WorldAssignmentDecision
	{
		eWorldAssignmentStatus	status			= eWorldAssignmentStatus::Failed;
		eWorldAssignmentAction	action			= eWorldAssignmentAction::Reject;
		WorldId					targetWorldId	= INVALID_WORLD_ID;
		WorldKey				targetWorld		= INVALID_WORLD_KEY;
		WorldOptions			options			= {};
	};

	struct WorldAssignmentResult
	{
		eWorldAssignmentStatus	status		= eWorldAssignmentStatus::Failed;
		eWorldAssignmentAction	action		= eWorldAssignmentAction::Reject;
		WorldId					worldId		= INVALID_WORLD_ID;
		WorldKey				targetWorld	= INVALID_WORLD_KEY;

		bool IsAssigned() const { return status == eWorldAssignmentStatus::Assigned && worldId != INVALID_WORLD_ID; }
	};
}
