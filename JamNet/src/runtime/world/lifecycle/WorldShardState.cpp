#include "pch.h"
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
		if (const auto* object = worldsById.FindAny(worldId))
		{
			UnregisterWorld(object->GetWorldId());
		}
		else
		{
			auto it = std::ranges::find_if(worldRecordsById, [worldId](const auto& pair)
				{
					return pair.first == worldId;
				});
			if (it != worldRecordsById.end())
				UnregisterWorld(it->first);
		}
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

	bool WorldShardState::ReserveEnter(WorldTransitionToken token, uint64 userId, const WorldRef& target)
	{
		if (!token.IsValid() || userId == 0 || !target.IsValid() || transitionMembers.contains(token))
			return false;
		WorldRecord* entry = FindAuthoritativeWorldEntry(target.worldId);
		if (!entry || !entry->HasCapacity() || entry->state == eWorldRuntimeState::Draining || entry->state == eWorldRuntimeState::Destroying)
			return false;

		++entry->memberCount;
		++entry->pendingAttachCount;
		++entry->lifecyclePinCount;
		transitionMembers.emplace(token, WorldTransitionMember
			{
				.token = token,
				.userId = userId,
				.worldId = target.worldId,
			});
		return true;
	}

	bool WorldShardState::PrepareEnter(WorldTransitionToken token)
	{
		auto it = transitionMembers.find(token);
		if (it == transitionMembers.end() || it->second.state != eWorldTransitionMemberState::Reserved)
			return false;
		it->second.state = eWorldTransitionMemberState::Prepared;
		return true;
	}

	bool WorldShardState::AttachPrepared(WorldTransitionToken token, const WorldUserContext& user)
	{
		auto it = transitionMembers.find(token);
		if (it == transitionMembers.end() || it->second.state != eWorldTransitionMemberState::Prepared)
			return false;

		auto* host = dynamic_cast<WorldMembershipHost*>(FindWorld(it->second.worldId));
		if (!host || !host->AddMember(user))
			return false;
		
		it->second.hostMemberAttached = true;
		it->second.state = eWorldTransitionMemberState::AttachedPendingCommit;
		return true;
	}

	bool WorldShardState::ActivateAttached(WorldTransitionToken token)
	{
		auto it = transitionMembers.find(token);
		if (it == transitionMembers.end() || it->second.state != eWorldTransitionMemberState::AttachedPendingCommit)
			return false;
		WorldRecord* entry = FindAuthoritativeWorldEntry(it->second.worldId);
		if (!entry)
			return false;
		entry->pendingAttachCount = entry->pendingAttachCount > 0 ? entry->pendingAttachCount - 1 : 0;
		entry->lifecyclePinCount = entry->lifecyclePinCount > 0 ? entry->lifecyclePinCount - 1 : 0;
		it->second.state = eWorldTransitionMemberState::Active;
		transitionMembers.erase(it);
		return true;
	}

	bool WorldShardState::RollbackEnter(WorldTransitionToken token)
	{
		auto it = transitionMembers.find(token);
		if (it == transitionMembers.end())
			return false;
		WorldRecord* entry = FindAuthoritativeWorldEntry(it->second.worldId);
		if (it->second.hostMemberAttached)
		{
			if (auto* serverWorld = dynamic_cast<ServerWorld*>(FindWorld(it->second.worldId)))
				serverWorld->RollbackMemberContent(it->second.userId, token);
			if (auto* host = dynamic_cast<WorldMembershipHost*>(FindWorld(it->second.worldId)))
				host->RemoveMember(it->second.userId);
		}
		if (entry)
		{
			entry->memberCount = entry->memberCount > 0 ? entry->memberCount - 1 : 0;
			entry->pendingAttachCount = entry->pendingAttachCount > 0 ? entry->pendingAttachCount - 1 : 0;
			entry->lifecyclePinCount = entry->lifecyclePinCount > 0 ? entry->lifecyclePinCount - 1 : 0;
		}
		transitionMembers.erase(it);
		return true;
	}

	bool WorldShardState::PrepareLeave(WorldTransitionToken token, uint64 userId, const WorldRef& source)
	{
		if (!token.IsValid() || userId == 0 || !source.IsValid() || transitionMembers.contains(token))
			return false;
		WorldRecord* entry = FindAuthoritativeWorldEntry(source.worldId);
		if (!entry || entry->state == eWorldRuntimeState::Draining || entry->state == eWorldRuntimeState::Destroying)
			return false;
		++entry->lifecyclePinCount;
		transitionMembers.emplace(token, WorldTransitionMember
			{
				.token		= token,
				.userId		= userId,
				.worldId	= source.worldId,
				.state		= eWorldTransitionMemberState::Active,
				.hostMemberAttached = true,
			});
		return true;
	}

	bool WorldShardState::DetachMain(WorldTransitionToken token)
	{
		auto it = transitionMembers.find(token);
		if (it == transitionMembers.end() || it->second.state != eWorldTransitionMemberState::Active)
			return false;
		
		auto* host = dynamic_cast<WorldMembershipHost*>(FindWorld(it->second.worldId));
		if (!host || !host->RemoveMember(it->second.userId))
			return false;

		if (WorldRecord* entry = FindAuthoritativeWorldEntry(it->second.worldId))
		{
			entry->memberCount = entry->memberCount > 0 ? entry->memberCount - 1 : 0;
		}
		
		it->second.hostMemberAttached = false;
		it->second.state = eWorldTransitionMemberState::DetachedPendingCommit;
		return true;
	}

	bool WorldShardState::CommitLeave(WorldTransitionToken token)
	{
		auto it = transitionMembers.find(token);
		if (it == transitionMembers.end() || it->second.state != eWorldTransitionMemberState::DetachedPendingCommit)
			return false;
		if (auto* serverWorld = dynamic_cast<ServerWorld*>(FindWorld(it->second.worldId));
			serverWorld && !serverWorld->CommitMemberLeave(it->second.userId, token))
		{
			return false;
		}
		if (WorldRecord* entry = FindAuthoritativeWorldEntry(it->second.worldId))
			entry->lifecyclePinCount = entry->lifecyclePinCount > 0 ? entry->lifecyclePinCount - 1 : 0;
		transitionMembers.erase(it);
		return true;
	}

	bool WorldShardState::CancelPreparedLeave(WorldTransitionToken token)
	{
		auto it = transitionMembers.find(token);
		if (it == transitionMembers.end() || it->second.state != eWorldTransitionMemberState::Active)
			return false;
		if (WorldRecord* entry = FindAuthoritativeWorldEntry(it->second.worldId))
			entry->lifecyclePinCount = entry->lifecyclePinCount > 0 ? entry->lifecyclePinCount - 1 : 0;
		transitionMembers.erase(it);
		return true;
	}

	bool WorldShardState::RestoreDetachedMain(WorldTransitionToken token, const WorldUserContext& user)
	{
		auto it = transitionMembers.find(token);
		if (it == transitionMembers.end() || it->second.state != eWorldTransitionMemberState::DetachedPendingCommit)
			return false;
		auto* host = dynamic_cast<WorldMembershipHost*>(FindWorld(it->second.worldId));
		if (!host || !host->AddMember(user))
			return false;
		if (auto* serverWorld = dynamic_cast<ServerWorld*>(host);
			serverWorld && !serverWorld->RestoreMemberContent(user, token))
		{
			host->RemoveMember(user.userId);
			return false;
		}
		if (WorldRecord* entry = FindAuthoritativeWorldEntry(it->second.worldId))
		{
			++entry->memberCount;
			entry->lifecyclePinCount = entry->lifecyclePinCount > 0 ? entry->lifecyclePinCount - 1 : 0;
		}
		transitionMembers.erase(it);
		return true;
	}

	bool WorldShardState::DisconnectMember(uint64 userId, const WorldRef& world)
	{
		if (userId == kInvalidUserId || !world.IsValid())
			return false;

		auto* host = dynamic_cast<WorldMembershipHost*>(FindWorld(world.worldId));
		if (!host)
			return false;
		if (auto* serverWorld = dynamic_cast<ServerWorld*>(host))
			serverWorld->CommitMemberLeave(userId, {});
		if (!host->RemoveMember(userId))
			return false;

		if (WorldRecord* entry = FindAuthoritativeWorldEntry(world.worldId))
		{
			entry->memberCount = entry->memberCount > 0 ? entry->memberCount - 1 : 0;
		}

		return true;
	}

	bool WorldShardState::TryBeginWorldDrain(WorldId worldId, uint64 expectedDestroyRevision)
	{
		WorldRecord* entry = FindAuthoritativeWorldEntry(worldId);
		if (!entry || entry->destroyRevision != expectedDestroyRevision || entry->lifecyclePinCount != 0 || entry->memberCount != 0 || entry->pendingAttachCount != 0)
			return false;

		entry->state = eWorldRuntimeState::Draining;
		++entry->destroyRevision;
		return true;
	}

	bool WorldShardState::CanDestroyWorld(WorldId worldId, uint64 expectedDestroyRevision) const
	{
		const WorldRecord* entry = FindAuthoritativeWorldEntry(worldId);
		return entry && entry->state == eWorldRuntimeState::Draining
			&& entry->destroyRevision == expectedDestroyRevision
			&& entry->lifecyclePinCount == 0 && entry->memberCount == 0 && entry->pendingAttachCount == 0;
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


}
