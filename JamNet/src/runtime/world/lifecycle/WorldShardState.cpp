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

	void WorldShardState::FreeWorldId(WorldId runtimeId)
	{
		if (const auto* object = worldsById.FindAny(runtimeId))
		{
			UnregisterWorld(object->GetWorldId());
		}
		else
		{
			auto it = std::ranges::find_if(authoritativeWorldsByRuntimeId, [runtimeId](const auto& pair)
				{
					return pair.first == runtimeId;
				});
			if (it != authoritativeWorldsByRuntimeId.end())
				UnregisterWorld(it->first);
		}
		worldsById.FreeId(runtimeId);
	}

	bool WorldShardState::AdoptWorld(std::unique_ptr<WorldBase> world)
	{
		return worldsById.Adopt(std::move(world));
	}

	bool WorldShardState::BeginDestroyWorld(WorldId runtimeId, eMailboxCloseMode mode, std::function<void()> onDestroyed)
	{
		if (!FindWorld(runtimeId))
			return false;

		WorldRecord* record = FindAuthoritativeWorldEntry(runtimeId);
		if (!record || record->state == eWorldRuntimeState::Destroying)
			return false;

		const eWorldRuntimeState previous = record->state;
		if (record->state != eWorldRuntimeState::Draining)
			record->state = eWorldRuntimeState::Draining;
		record->state = eWorldRuntimeState::Destroying;
		const bool accepted = worldsById.BeginDestroy(runtimeId, mode, [this, onDestroyed = std::move(onDestroyed)](RuntimeId id)
			{
				FreeWorldId(static_cast<WorldId>(id));
				if (onDestroyed)
					onDestroyed();
			});
		if (!accepted)
			record->state = previous;
		return accepted;
	}

	WorldBase* WorldShardState::FindWorld(WorldId runtimeId)
	{
		return worldsById.Find(runtimeId);
	}

	const WorldBase* WorldShardState::FindWorld(WorldId runtimeId) const
	{
		return worldsById.Find(runtimeId);
	}

	ShardOwnedObjectRefSlot<WorldBase> WorldShardState::FindWorldRef(WorldId runtimeId)
	{
		return worldsById.FindRef(runtimeId);
	}

	ShardOwnedObjectRefSlot<const WorldBase> WorldShardState::FindWorldRef(WorldId runtimeId) const
	{
		return worldsById.FindRef(runtimeId);
	}

	void WorldShardState::RegisterWorld(const WorldConfig& config)
	{
		if (!config.HasRuntime())
			return;

		UnregisterWorld(config.world.worldId);

		WorldRecord entry{};
		entry.instance	= config.world.instance;
		entry.runtime	= config.RuntimeRef();
		entry.group		= config.templateData.group;
		entry.capacity	= config.templateData.capacity;
		entry.state		= eWorldRuntimeState::Standby;

		authoritativeWorldsByRuntimeId[config.world.worldId] = entry;
		worldsByArchetypeKey.emplace(config.world.instance.archetypeKey, config.world.worldId);
		if (config.templateData.group != kInvalidWorldGroup)
			worldsByGroup.emplace(config.templateData.group, config.world.worldId);
	}

	void WorldShardState::UnregisterWorld(WorldId runtimeId)
	{
		if (runtimeId == kInvalidWorldId)
			return;

		authoritativeWorldsByRuntimeId.erase(runtimeId);
		std::erase_if(worldsByArchetypeKey, [runtimeId](const auto& pair)
			{
				return pair.second == runtimeId;
			});
		std::erase_if(worldsByGroup, [runtimeId](const auto& pair)
			{
				return pair.second == runtimeId;
			});
	}

	WorldRecord* WorldShardState::FindAuthoritativeWorldEntry(WorldId runtimeId)
	{
		auto it = authoritativeWorldsByRuntimeId.find(runtimeId);
		return (it != authoritativeWorldsByRuntimeId.end()) ? &it->second : nullptr;
	}

	const WorldRecord* WorldShardState::FindAuthoritativeWorldEntry(WorldId runtimeId) const
	{
		auto it = authoritativeWorldsByRuntimeId.find(runtimeId);
		return (it != authoritativeWorldsByRuntimeId.end()) ? &it->second : nullptr;
	}

	bool WorldShardState::TryReserveMemberSlot(WorldId runtimeId)
	{
		WorldRecord* entry = FindAuthoritativeWorldEntry(runtimeId);
		if (!entry || !entry->HasCapacity())
			return false;

		++entry->memberCount;
		RefreshRuntimeState(runtimeId);
		return true;
	}

	void WorldShardState::ReleaseMemberSlot(WorldId runtimeId)
	{
		WorldRecord* entry = FindAuthoritativeWorldEntry(runtimeId);
		if (!entry || entry->memberCount == 0)
			return;

		--entry->memberCount;
		entry->activePresenceCount = std::min(entry->activePresenceCount, entry->memberCount);
		RefreshRuntimeState(runtimeId);
	}

	void WorldShardState::PromoteMemberPresence(WorldId runtimeId)
	{
		WorldRecord* entry = FindAuthoritativeWorldEntry(runtimeId);
		if (!entry || entry->state == eWorldRuntimeState::Draining || entry->state == eWorldRuntimeState::Destroying)
			return;

		if (entry->activePresenceCount < entry->memberCount)
			++entry->activePresenceCount;
		RefreshRuntimeState(runtimeId);
	}

	void WorldShardState::DemoteMemberPresence(WorldId runtimeId)
	{
		WorldRecord* entry = FindAuthoritativeWorldEntry(runtimeId);
		if (!entry || entry->activePresenceCount == 0)
			return;

		--entry->activePresenceCount;
		RefreshRuntimeState(runtimeId);
	}

	void WorldShardState::RefreshRuntimeState(WorldId runtimeId)
	{
		WorldRecord* entry = FindAuthoritativeWorldEntry(runtimeId);
		if (!entry || entry->state == eWorldRuntimeState::Draining || entry->state == eWorldRuntimeState::Destroying)
			return;

		if (entry->activePresenceCount > 0)
		{
			entry->state = eWorldRuntimeState::Active;
			return;
		}

		if (entry->state == eWorldRuntimeState::Active || entry->state == eWorldRuntimeState::Paused)
		{
			entry->state = eWorldRuntimeState::Paused;
			return;
		}

		entry->state = eWorldRuntimeState::Standby;
	}

	bool WorldShardState::ReserveEnter(WorldTransitionToken token, uint64 userId, const WorldRuntimeRef& target)
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
		RefreshRuntimeState(target.worldId);
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
		++entry->activePresenceCount;
		entry->lifecyclePinCount = entry->lifecyclePinCount > 0 ? entry->lifecyclePinCount - 1 : 0;
		it->second.state = eWorldTransitionMemberState::Active;
		RefreshRuntimeState(it->second.worldId);
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
			RefreshRuntimeState(it->second.worldId);
		}
		transitionMembers.erase(it);
		return true;
	}

	bool WorldShardState::PrepareLeave(WorldTransitionToken token, uint64 userId, const WorldRuntimeRef& source)
	{
		if (!token.IsValid() || userId == 0 || !source.IsValid() || transitionMembers.contains(token))
			return false;
		WorldRecord* entry = FindAuthoritativeWorldEntry(source.worldId);
		if (!entry || entry->activePresenceCount == 0)
			return false;
		++entry->lifecyclePinCount;
		transitionMembers.emplace(token, WorldTransitionMember
			{
				.token = token,
				.userId = userId,
				.worldId = source.worldId,
				.state = eWorldTransitionMemberState::Active,
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
		WorldRecord* entry = FindAuthoritativeWorldEntry(it->second.worldId);
		if (entry)
		{
			entry->activePresenceCount = entry->activePresenceCount > 0 ? entry->activePresenceCount - 1 : 0;
			entry->memberCount = entry->memberCount > 0 ? entry->memberCount - 1 : 0;
			RefreshRuntimeState(it->second.worldId);
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
			++entry->activePresenceCount;
			entry->lifecyclePinCount = entry->lifecyclePinCount > 0 ? entry->lifecyclePinCount - 1 : 0;
			RefreshRuntimeState(it->second.worldId);
		}
		transitionMembers.erase(it);
		return true;
	}

	bool WorldShardState::DisconnectMember(uint64 userId, const WorldRuntimeRef& runtime)
	{
		if (userId == kInvalidUserId || !runtime.IsValid())
			return false;

		auto* host = dynamic_cast<WorldMembershipHost*>(FindWorld(runtime.worldId));
		if (!host)
			return false;
		if (auto* serverWorld = dynamic_cast<ServerWorld*>(host))
			serverWorld->CommitMemberLeave(userId, {});
		if (!host->RemoveMember(userId))
			return false;

		if (WorldRecord* entry = FindAuthoritativeWorldEntry(runtime.worldId))
		{
			entry->activePresenceCount = entry->activePresenceCount > 0 ? entry->activePresenceCount - 1 : 0;
			entry->memberCount = entry->memberCount > 0 ? entry->memberCount - 1 : 0;
			RefreshRuntimeState(runtime.worldId);
		}

		return true;
	}

	bool WorldShardState::TryBeginRuntimeDrain(WorldId runtimeId, uint64 expectedDestroyRevision)
	{
		WorldRecord* entry = FindAuthoritativeWorldEntry(runtimeId);
		if (!entry || entry->destroyRevision != expectedDestroyRevision || entry->lifecyclePinCount != 0
			|| entry->memberCount != 0 || entry->pendingAttachCount != 0)
			return false;
		entry->state = eWorldRuntimeState::Draining;
		++entry->destroyRevision;
		return true;
	}

	bool WorldShardState::CanDestroyRuntime(WorldId runtimeId, uint64 expectedDestroyRevision) const
	{
		const WorldRecord* entry = FindAuthoritativeWorldEntry(runtimeId);
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
