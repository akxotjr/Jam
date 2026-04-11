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

	enum class eWorldRequestAction : uint8
	{
		AutoAssign	= 0,
		Join		= 1,
		Leave		= 2,
		Transfer	= 3,
	};

	enum class eWorldTransferOutcome : uint8
	{
		Succeeded	= 0,
		Failed		= 1,
		InDoubt		= 2,
	};

	enum class eWorldTransferReason : uint8
	{
		None				= 0,
		InvalidArgument		= 1,
		AlreadyInTarget		= 2,
		TargetUnavailable	= 3,
		CapacityExceeded	= 4,
		ConflictingTransfer	= 5,
		Timeout				= 6,
		MailboxClosed		= 7,
		Shutdown			= 8,
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

	struct WorldTransferResult
	{
		eWorldTransferOutcome	outcome			= eWorldTransferOutcome::Failed;
		eWorldTransferReason	reason			= eWorldTransferReason::None;
		WorldId					sourceWorldId	= INVALID_WORLD_ID;
		WorldId					targetWorldId	= INVALID_WORLD_ID;

		bool Succeeded() const { return outcome == eWorldTransferOutcome::Succeeded; }
		bool Failed() const { return outcome == eWorldTransferOutcome::Failed; }
		bool InDoubt() const { return outcome == eWorldTransferOutcome::InDoubt; }
	};

	struct ClientBindState
	{
		bool	tcpBound	= false;
		bool	udpBound	= false;
		bool	ready		= false;
	};

	inline constexpr eWorldAssignmentStatus ToWorldAssignmentStatus(uint8 raw) noexcept
	{
		switch (raw)
		{
		case static_cast<uint8>(eWorldAssignmentStatus::Assigned):
			return eWorldAssignmentStatus::Assigned;
		case static_cast<uint8>(eWorldAssignmentStatus::Waiting):
			return eWorldAssignmentStatus::Waiting;
		default:
			return eWorldAssignmentStatus::Failed;
		}
	}

	inline constexpr eWorldRequestAction ToWorldRequestAction(uint8 raw) noexcept
	{
		switch (raw)
		{
		case static_cast<uint8>(eWorldRequestAction::Join):
			return eWorldRequestAction::Join;
		case static_cast<uint8>(eWorldRequestAction::Leave):
			return eWorldRequestAction::Leave;
		case static_cast<uint8>(eWorldRequestAction::Transfer):
			return eWorldRequestAction::Transfer;
		default:
			return eWorldRequestAction::AutoAssign;
		}
	}

	inline constexpr eWorldAssignmentAction ToWorldAssignmentAction(uint8 raw) noexcept
	{
		switch (raw)
		{
		case static_cast<uint8>(eWorldAssignmentAction::Join):
			return eWorldAssignmentAction::Join;
		case static_cast<uint8>(eWorldAssignmentAction::Provision):
			return eWorldAssignmentAction::Provision;
		case static_cast<uint8>(eWorldAssignmentAction::Transfer):
			return eWorldAssignmentAction::Transfer;
		case static_cast<uint8>(eWorldAssignmentAction::Reject):
			return eWorldAssignmentAction::Reject;
		default:
			return eWorldAssignmentAction::None;
		}
	}

	inline constexpr eWorldTransferReason ToWorldTransferReason(uint8 raw) noexcept
	{
		switch (raw)
		{
		case static_cast<uint8>(eWorldTransferReason::InvalidArgument):
			return eWorldTransferReason::InvalidArgument;
		case static_cast<uint8>(eWorldTransferReason::AlreadyInTarget):
			return eWorldTransferReason::AlreadyInTarget;
		case static_cast<uint8>(eWorldTransferReason::TargetUnavailable):
			return eWorldTransferReason::TargetUnavailable;
		case static_cast<uint8>(eWorldTransferReason::CapacityExceeded):
			return eWorldTransferReason::CapacityExceeded;
		case static_cast<uint8>(eWorldTransferReason::ConflictingTransfer):
			return eWorldTransferReason::ConflictingTransfer;
		case static_cast<uint8>(eWorldTransferReason::Timeout):
			return eWorldTransferReason::Timeout;
		case static_cast<uint8>(eWorldTransferReason::MailboxClosed):
			return eWorldTransferReason::MailboxClosed;
		case static_cast<uint8>(eWorldTransferReason::Shutdown):
			return eWorldTransferReason::Shutdown;
		default:
			return eWorldTransferReason::None;
		}
	}
}
