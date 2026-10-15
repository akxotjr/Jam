#include "pch.h"
#include "jamnet/core/executor/GlobalExecutor.h"
#include "jamnet/core/executor/ShardExecutor.h"
#include "jamnet/runtime/world/lifecycle/WorldShardState.h"
#include "jamnet/runtime/world/lifecycle/WorldBase.h"
#include "jamnet/runtime/world/simulation/server/ServerWorld.h"

namespace jam::net
{
	WorldId WorldShardState::AllocWorldId()
	{
		return static_cast<WorldId>(worldsById.AllocId(shardIndex));
	}

	void WorldShardState::FreeWorldId(WorldId worldId)
	{
		UnregisterWorld(worldId);
		worldsById.FreeId(worldId);
	}

	bool WorldShardState::AdoptWorld(std::unique_ptr<WorldBase> world)
	{
		return worldsById.Adopt(std::move(world));
	}

	bool WorldShardState::BeginDestroyWorld(WorldId worldId, eMailboxCloseMode mode, std::function<void()> onDestroyed)
	{
		if (!FindWorld(worldId))
			return false;

		WorldRecord* record = FindAuthoritativeWorldEntry(worldId);
		if (!record || record->state == eWorldRuntimeState::Destroying)
			return false;

		const eWorldRuntimeState previous = record->state;
		if (record->state != eWorldRuntimeState::Draining)
			record->state = eWorldRuntimeState::Draining;

		record->state = eWorldRuntimeState::Destroying;
		const bool accepted = worldsById.BeginDestroy(worldId, mode, [this, onDestroyed = std::move(onDestroyed)](RuntimeId id)
			{
				FreeWorldId(static_cast<WorldId>(id));
				if (onDestroyed)
					onDestroyed();
			});
		if (!accepted)
			record->state = previous;
		return accepted;
	}

	WorldBase* WorldShardState::FindWorld(WorldId worldId)
	{
		return worldsById.Find(worldId);
	}

	const WorldBase* WorldShardState::FindWorld(WorldId worldId) const
	{
		return worldsById.Find(worldId);
	}

	ShardOwnedObjectRefSlot<WorldBase> WorldShardState::FindWorldRef(WorldId worldId)
	{
		return worldsById.FindRef(worldId);
	}

	ShardOwnedObjectRefSlot<const WorldBase> WorldShardState::FindWorldRef(WorldId worldId) const
	{
		return worldsById.FindRef(worldId);
	}

	void WorldShardState::RegisterWorld(const WorldConfig& config)
	{
		if (!config.HasWorld())
			return;

		UnregisterWorld(config.world.worldId);

		WorldRecord entry{};
		entry.instance	= config.world.instance;
		entry.world	= config.GetWorldRef();
		entry.group		= config.templateData.group;
		entry.capacity	= config.templateData.capacity;
		entry.state		= eWorldRuntimeState::Standby;

		worldRecordsById[config.world.worldId] = entry;
	}

	void WorldShardState::UnregisterWorld(WorldId worldId)
	{
		if (worldId == kInvalidWorldId)
			return;

		worldRecordsById.erase(worldId);
	}

	WorldRecord* WorldShardState::FindAuthoritativeWorldEntry(WorldId worldId)
	{
		auto it = worldRecordsById.find(worldId);
		return (it != worldRecordsById.end()) ? &it->second : nullptr;
	}

	const WorldRecord* WorldShardState::FindAuthoritativeWorldEntry(WorldId worldId) const
	{
		auto it = worldRecordsById.find(worldId);
		return (it != worldRecordsById.end()) ? &it->second : nullptr;
	}


	WorldShardState& GetOrCreateWorldShardState(jam::ShardLocal& local)
	{
		if (!local.worldState)
		{
			local.worldState = std::make_shared<WorldShardState>();
			local.worldState->shardIndex = local.shardIndex;
		}

		return *local.worldState;
	}

	namespace
	{
		ServerWorld* FindMembershipWorld(WorldShardState& state, const WorldRef& world)
		{
			auto* server = dynamic_cast<ServerWorld*>(state.FindWorld(world.worldId));
			return server && server->GetWorldRef() == world ? server : nullptr;
		}
	}

	eWorldTransitionFailure WorldShardState::JoinWorldMembership(
		const WorldRef& world,
		WorldUserContext user,
		WorldTransitionToken token,
		std::string entryPoint)
	{
		auto* entry = FindAuthoritativeWorldEntry(world.worldId);
		auto* server = FindMembershipWorld(*this, world);

		if (!entry || !server
			|| entry->state == eWorldRuntimeState::Draining
			|| entry->state == eWorldRuntimeState::Destroying)
		{
			return eWorldTransitionFailure::WorldDestroyed;
		}

		// Reject existing work before adjusting its membership accounting.
		if (server->HasMembershipEntry(token) || server->HasDetachedMembership(token))
			return eWorldTransitionFailure::InvalidRequest;

		if (!entry->HasMembershipCapacity())
			return eWorldTransitionFailure::CapacityExceeded;

		++entry->memberCount;
		++entry->lifecyclePinCount;

		const auto failure = server->JoinMembership(std::move(user), token, std::move(entryPoint));

		// Failed cleanup retains the entry and its pin for the cancellation retry.
		if (!server->HasMembershipEntry(token))
		{
			--entry->memberCount;
			--entry->lifecyclePinCount;
		}

		return failure;
	}

	bool WorldShardState::DetachWorldMembership(const WorldRef& world, UserId userId, WorldTransitionToken token)
	{
		auto* record = FindAuthoritativeWorldEntry(world.worldId);
		auto* server = FindMembershipWorld(*this, world);
		if (!record || !server || record->memberCount == 0
			|| record->state == eWorldRuntimeState::Draining || record->state == eWorldRuntimeState::Destroying)
			return false;
		if (!server->DetachMembership(userId, token)) return false;
		--record->memberCount;
		++record->recoveryMemberCount;
		++record->lifecyclePinCount;
		return true;
	}

	bool WorldShardState::CancelWorldMembership(const WorldRef& world, UserId userId, WorldTransitionToken token)
	{
		auto* server = FindMembershipWorld(*this, world);
		auto* record = FindAuthoritativeWorldEntry(world.worldId);
		// The unreserved target can disappear during client preparation, before attachment.
		if (!server) return true;
		if (!record) return false;
		if (!server->HasMembershipEntry(token)) return true; // No attachment was made.
		if (!server->CancelMembership(userId, token)) return false;
		JAM_ASSERT(record->memberCount > 0 && record->lifecyclePinCount > 0);
		--record->memberCount;
		--record->lifecyclePinCount;
		return true;
	}

	bool WorldShardState::RecoverWorldMembership(const WorldRef& world, WorldUserContext user, WorldTransitionToken token)
	{
		auto* server = FindMembershipWorld(*this, world);
		auto* record = FindAuthoritativeWorldEntry(world.worldId);
		if (!server || !record || !server->RecoverMembership(std::move(user), token)) return false;
		JAM_ASSERT(record->recoveryMemberCount > 0 && record->lifecyclePinCount > 0);
		++record->memberCount;
		--record->recoveryMemberCount;
		--record->lifecyclePinCount;
		return true;
	}

	bool WorldShardState::CompleteWorldMembershipLeave(const WorldRef& world, UserId userId, WorldTransitionToken token)
	{
		auto* server = FindMembershipWorld(*this, world);
		auto* record = FindAuthoritativeWorldEntry(world.worldId);
		if (!server || !record || !server->CompleteMembershipLeave(userId, token)) return false;
		JAM_ASSERT(record->recoveryMemberCount > 0 && record->lifecyclePinCount > 0);
		--record->recoveryMemberCount;
		--record->lifecyclePinCount;
		return true;
	}

	bool WorldShardState::CompleteWorldMembershipEntry(const WorldRef& world, UserId userId, WorldTransitionToken token)
	{
		auto* server = FindMembershipWorld(*this, world);
		auto* record = FindAuthoritativeWorldEntry(world.worldId);
		if (!server || !record || !server->CompleteMembershipEntry(userId, token)) return false;
		JAM_ASSERT(record->lifecyclePinCount > 0);
		--record->lifecyclePinCount;
		return true;
	}

	bool WorldShardState::BeginDestroyMembershipWorld(WorldId worldId, eMailboxCloseMode mode, std::function<void()> onDestroyed)
	{
		const auto* record = FindAuthoritativeWorldEntry(worldId);
		const auto* server = dynamic_cast<const ServerWorld*>(FindWorld(worldId));
		if ((record && record->lifecyclePinCount != 0) || (server && server->HasMembershipWork()))
			return false;
		return BeginDestroyWorld(worldId, mode, std::move(onDestroyed));
	}
}
