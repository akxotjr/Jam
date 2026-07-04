#pragma once

#include <jambase/EnumUtils.h>
#include <jambase/SmallHash.h>

#include "jamnet/core/executor/RuntimeId.h"
#include "jamnet/runtime/schema/gen/world_assignment_generated.h"
#include "jamnet/runtime/world/types/WorldTemplateKey.h"
#include "jamnet/runtime/world/data/WorldArchetypeDatabase.h"

#include <functional>
#include <string>
#include <vector>

namespace jam::net
{
	using NetWorldId	 = RuntimeId;
	using LocalWorldId   = RuntimeId;
	using WorldGroup     = uint64;

	inline constexpr NetWorldId		kInvalidNetWorldId = kInvalidRuntimeId;
	inline constexpr LocalWorldId	kInvalidLocalWorldId = kInvalidRuntimeId;
	inline constexpr WorldGroup		kInvalidWorldGroup = 0;
	inline constexpr WorldGroup		kDefaultWorldGroup = 1;

	inline NetWorldId MakeWorldId(uint16 shardIndex, uint16 localIndex, uint32 generation)
	{
		return MakeRuntimeId(shardIndex, localIndex, generation);
	}

	inline uint16 GetWorldShardIndex(NetWorldId worldId)
	{
		return GetRuntimeShardIndex(worldId);
	}

	inline uint16 GetWorldLocalIndex(NetWorldId worldId)
	{
		return GetRuntimeLocalIndex(worldId);
	}

	inline uint32 GetWorldGeneration(NetWorldId worldId)
	{
		return GetRuntimeGeneration(worldId);
	}

	inline LocalWorldId MakeLocalWorldId(uint16 shardIndex, uint16 localIndex, uint32 generation)
	{
		return MakeRuntimeId(shardIndex, localIndex, generation);
	}

	inline uint16 GetLocalWorldShardIndex(LocalWorldId worldId)
	{
		return GetRuntimeShardIndex(worldId);
	}

	inline uint16 GetLocalWorldLocalIndex(LocalWorldId worldId)
	{
		return GetRuntimeLocalIndex(worldId);
	}

	inline uint32 GetLocalWorldGeneration(LocalWorldId worldId)
	{
		return GetRuntimeGeneration(worldId);
	}





	enum class eWorldKind : uint8
	{
		Physical	= 0,
		Virtual		= 1,
	};


	enum class eWorldTickMode : uint8
	{
		None		= 0,
		Fixed		= 1,
		OnDemand	= 2,
	};

	enum class eWorldRole : uint8
	{
		None		= 0,
		Main		= 1,
		Auxiliary	= 2,
	};

	enum class eWorldMembershipPresence : uint8
	{
		None		= 0,
		Passive		= 1,
		Active		= 2,
	};

	enum class ePhysicalWorldRuntimeState : uint8
	{
		Standby		= 0,
		Active		= 1,
		Paused		= 2,
		Closing		= 3,
	};

	enum class eWorldRoutePolicy : uint8
	{
		SpreadByLoad		= 0,
		PreferredShard		= 1,
		DedicatedShard		= 2,
		CoLocateWithWorld	= 3,
	};

	struct WorldRouteConfig
	{
		eWorldRoutePolicy	policy			= eWorldRoutePolicy::SpreadByLoad;
		uint32				preferredShard	= 0;
		NetWorldId			colocateWorldId = kInvalidNetWorldId;
		bool				hardAffinity	= false;
	};


	struct WorldKey
	{
		WorldArchetypeKey	archetypeKey;
		NetWorldId			worldId	= kInvalidNetWorldId;

		bool IsValid()  const { return IsValidAssetKey(archetypeKey); }
		bool IsIssued() const { return IsValid() && worldId != kInvalidNetWorldId; }

		bool operator==(const WorldKey&) const = default;
	};


	struct WorldKeyHash
	{
		size_t operator()(const WorldKey& key) const noexcept
		{
			return SmallHashOf(key.archetypeKey.v, key.worldId);
		}
	};

	// User-local view of one joined world.
	struct WorldMembership
	{
		WorldKey					key			 = {};
		LocalWorldId				localWorldId = kInvalidLocalWorldId;
		eWorldKind					kind		 = eWorldKind::Physical;
		eWorldRole					role		 = eWorldRole::None;
		eWorldMembershipPresence	presence	 = eWorldMembershipPresence::None;

		bool IsValid() const { return key.IsValid(); }
	};

	inline WorldMembership* FindWorldMembership(std::vector<WorldMembership>& worlds, const WorldKey& key)
	{
		auto it = std::ranges::find(worlds, key, &WorldMembership::key);
		return (it != worlds.end()) ? &(*it) : nullptr;
	}

	inline const WorldMembership* FindWorldMembership(const std::vector<WorldMembership>& worlds, const WorldKey& key)
	{
		auto it = std::ranges::find(worlds, key, &WorldMembership::key);
		return (it != worlds.end()) ? &(*it) : nullptr;
	}

	inline WorldMembership* FindMainWorldMembership(std::vector<WorldMembership>& worlds)
	{
		auto it = std::ranges::find(worlds, eWorldRole::Main, &WorldMembership::role);
		return (it != worlds.end()) ? &(*it) : nullptr;
	}

	inline const WorldMembership* FindMainWorldMembership(const std::vector<WorldMembership>& worlds)
	{
		auto it = std::ranges::find(worlds, eWorldRole::Main, &WorldMembership::role);
		return (it != worlds.end()) ? &(*it) : nullptr;
	}

	inline uint32 CountMainWorldMemberships(const std::vector<WorldMembership>& worlds)
	{
		return static_cast<uint32>(std::ranges::count(worlds, eWorldRole::Main, &WorldMembership::role));
	}



	enum class eWorldAction : uint8
	{
		AutoAssign		= 0,
		Join			= 1,
		Leave			= 2,
		Transfer		= 3,
		Promote			= 4,
	};

	enum class eWorldActionExecFlag : uint8
	{
		None			= 0,
		CreateTarget	= 1 << 0,
		DestroySource	= 1 << 1,
	};
	using WorldActionExecFlags = FlagsT<eWorldActionExecFlag>;

	inline bool HasWorldActionExec(WorldActionExecFlags flags, eWorldActionExecFlag bit)
	{
		return flags.has_any(bit);
	}

	enum class eWorldActionStatus : uint8
	{
		Succeeded			= 0,
		Failed				= 2,
	};

	enum class eWorldActionReason : uint8
	{
		None				= 0,
		InvalidArgument		= 1,
		AlreadyInTarget		= 2,
		TargetUnavailable	= 3,
		CapacityExceeded	= 4,
		ConflictingTransfer = 5,
		Timeout				= 6,
		MailboxClosed		= 7,
		Shutdown			= 8,
	};


	// settings shared by all instances of the same world type.
	struct WorldTemplateData
	{
		std::string			name;
		WorldTemplateKey	key;
		eWorldKind			kind						 = eWorldKind::Physical;
		WorldGroup			group						 = kInvalidWorldGroup;
		bool				allowMultipleInstancePerUser = false;
		uint32				maxAuxiliaryWorldMemberships = 0;
		bool				persistent					 = true;
		bool				pauseWhenNoActivePresence	 = true;
		bool				destroyWhenEmpty			 = false;
		bool				isPrivate					 = false;
		uint64				standbyTTL_ns				 = 0_ns;
		uint64				pausedTTL_ns				 = 0_ns;
		uint32				capacity					 = 0;
		eWorldTickMode		tickMode					 = eWorldTickMode::Fixed;
		WorldRouteConfig	route						 = {};
		std::string			actorArchetypeSetPath;
		std::string			actorLevelPath;
		std::string			physicsAssetPath;
	};

	// Concrete instance config resolved from template + issued world key.
	struct WorldConfig
	{
		WorldKey			key	 = {};
		WorldTemplateData	templateData = {};

		bool IsValid() const { return key.IsValid(); }
		bool operator==(const WorldConfig& rhs) const
		{
			return key == rhs.key;
		}
	};

	struct WorldConfigHash
	{
		size_t operator()(const WorldConfig& config) const noexcept
		{
			return WorldKeyHash{}(config.key);
		}
	};


	enum class eWorldResolveMode : uint8
	{
		None			= 0,
		ExistingOnly	= 1,
		CreateIfMissing = 2,
		RejectIfMissing = 3,
	};

	struct WorldActionRequest;
	struct WorldActionResult;

	// Server-side execution method derived from one request.
	struct WorldActionPlan
	{
		eWorldActionStatus			status				= eWorldActionStatus::Failed;
		eWorldAction				action				= eWorldAction::AutoAssign;
		WorldActionExecFlags		execFlags			= {};
		eWorldActionReason			reason				= eWorldActionReason::None;
		eWorldMembershipPresence	sourcePresence		= eWorldMembershipPresence::None;
		eWorldMembershipPresence	resultingPresence	= eWorldMembershipPresence::None;
		WorldKey					source				= {};
		WorldKey					target				= {};

		WorldActionPlan() = default;
		explicit WorldActionPlan(const WorldActionRequest& req);

		bool Executable() const { return status == eWorldActionStatus::Succeeded; }
		bool Rejected()   const { return status == eWorldActionStatus::Failed; }
	};

	// Client/server request envelope.
	struct WorldActionRequest
	{
		uint64									principalId	= 0;
		eWorldAction							action		= eWorldAction::AutoAssign;
		WorldKey								source		= {};	
		WorldKey								target		= {};
		std::function<void(WorldActionResult)>	onResponse;

		static WorldActionRequest FromFb(const fb::fbWorldActionReq& req, uint64 userId = 0, std::function<void(WorldActionResult)> onResponse = {});
	};

	// Final action outcome delivered to requester and listeners.
	enum class eWorldMembershipDeltaOp : uint8
	{
		Upsert = 0,
		Remove = 1,
	};

	struct WorldMembershipDelta
	{
		eWorldMembershipDeltaOp	op		   = eWorldMembershipDeltaOp::Upsert;
		WorldMembership			membership = {};
	};

	struct WorldRuntimeDelta
	{
		WorldKey					key		= {};
		ePhysicalWorldRuntimeState	runtime = ePhysicalWorldRuntimeState::Standby;
	};

	struct WorldActionResult
	{
		eWorldActionStatus					status			 = eWorldActionStatus::Failed;
		eWorldActionReason					reason			 = eWorldActionReason::None;
		eWorldAction						action			 = eWorldAction::AutoAssign;
		WorldActionExecFlags				execFlags		 = {};
		WorldKey							source			 = {};
		WorldKey							target			 = {};
		std::vector<WorldMembershipDelta>	membershipDeltas = {};
		std::vector<WorldRuntimeDelta>		worldRuntimeDeltas = {};

		bool Succeeded()  const { return status == eWorldActionStatus::Succeeded; }
		bool Failed()	  const { return status == eWorldActionStatus::Failed; }
		bool IsAssigned() const { return Succeeded() && target.IsValid(); }

		flatbuffers::Offset<fb::fbWorldActionRes>	CreateFb(flatbuffers::FlatBufferBuilder& fbb) const;
	};

	struct WorldTransferPayload
	{
		virtual ~WorldTransferPayload() = default;
	};

	struct WorldTransferContext
	{
		uint64						userId			 = 0;
		WorldKey					source			 = {};
		WorldKey					target			 = {};
		eWorldMembershipPresence	sourcePresence	 = eWorldMembershipPresence::None;
		eWorldMembershipPresence	resultingPresence = eWorldMembershipPresence::None;
	};


	struct ClientBindState
	{
		bool	tcpBound	= false;
		bool	udpBound	= false;
		bool	ready		= false;
	};


	inline WorldActionRequest WorldActionRequest::FromFb(const fb::fbWorldActionReq& req, uint64 userId, std::function<void(WorldActionResult)> onResponse)
	{
		eWorldAction action = eWorldAction::AutoAssign;
		switch (req.action())
		{
		case fb::fbWorldAction_AutoAssign: action = eWorldAction::AutoAssign; break;
		case fb::fbWorldAction_Join: action = eWorldAction::Join; break;
		case fb::fbWorldAction_Leave: action = eWorldAction::Leave; break;
		case fb::fbWorldAction_Transfer: action = eWorldAction::Transfer; break;
		case fb::fbWorldAction_Promote: action = eWorldAction::Promote; break;
		default: break;
		}

		return WorldActionRequest
		{
			.principalId = userId,
			.action		 = action,
			.source		 = WorldKey
			{
				.archetypeKey = req.src_archetype_key(),
				.worldId	  = req.src_world_id()
			},
			.target	= WorldKey
			{
				.archetypeKey = req.target_archetype_key(),
				.worldId	  = req.target_world_id(),
			},
			.onResponse	= std::move(onResponse),
		};
	}

	inline WorldActionPlan::WorldActionPlan(const WorldActionRequest& req)
		: action(req.action)
		, source(req.source)
		, target(req.target)
	{
	}

	inline flatbuffers::Offset<fb::fbWorldActionRes> WorldActionResult::CreateFb(flatbuffers::FlatBufferBuilder& fbb) const
	{
		std::vector<flatbuffers::Offset<fb::fbWorldMembershipDelta>> membershipDeltaOffsets;
		membershipDeltaOffsets.reserve(membershipDeltas.size());
		for (const WorldMembershipDelta& delta : membershipDeltas)
		{
			const fb::fbWorldMembershipDeltaOp fbOp = delta.op == eWorldMembershipDeltaOp::Remove
				? fb::fbWorldMembershipDeltaOp_Remove
				: fb::fbWorldMembershipDeltaOp_Upsert;

			membershipDeltaOffsets.push_back(fb::CreatefbWorldMembershipDelta(
				fbb,
				fbOp,
				delta.membership.key.archetypeKey.v,
				delta.membership.key.worldId,
				ToUnderlying(delta.membership.kind),
				ToUnderlying(delta.membership.role),
				ToUnderlying(delta.membership.presence)));
		}

		const auto membershipDeltasOffset = membershipDeltaOffsets.empty()
			? 0
			: fbb.CreateVector(membershipDeltaOffsets);

		std::vector<flatbuffers::Offset<fb::fbWorldRuntimeDelta>> worldRuntimeDeltaOffsets;
		worldRuntimeDeltaOffsets.reserve(worldRuntimeDeltas.size());
		for (const WorldRuntimeDelta& delta : worldRuntimeDeltas)
		{
			worldRuntimeDeltaOffsets.push_back(fb::CreatefbWorldRuntimeDelta(
				fbb,
				delta.key.archetypeKey.v,
				delta.key.worldId,
				ToUnderlying(delta.runtime)));
		}

		const auto worldRuntimeDeltasOffset = worldRuntimeDeltaOffsets.empty()
			? 0
			: fbb.CreateVector(worldRuntimeDeltaOffsets);

		return fb::CreatefbWorldActionRes(
			fbb, 
			ToUnderlying(status), 
			ToUnderlying(reason), 
			ToUnderlying(action), 
			static_cast<uint8>(execFlags.bits()),
			source.archetypeKey.v,
			source.worldId,
			target.archetypeKey.v,
			target.worldId,
			membershipDeltasOffset,
			worldRuntimeDeltasOffset);
	}
}
