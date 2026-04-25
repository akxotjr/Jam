#include "pch.h"
#include "jamnet/sync/replication/ServerAoiSystem.h"

#include "jamnet/sync/networld/ServerNetWorld.h"
#include "jamnet/sync/replication/ServerPhysicsSystem.h"
#include "jamnet/sync/replication/NetWorldContext.h"

namespace jam::net
{
	ServerAoiSystem::ServerAoiSystem(entt::registry& world, px::IPhysicsFacade* physics)
		: m_world(world), m_physics(physics)
	{
	}

	void ServerAoiSystem::Init(const AoiConfig& cfg)
	{
		m_cfg = cfg;

		m_states.clear();
		m_alwaysVisible.clear();
		m_actorStates.clear();
		m_userStates.clear();
		m_cellActors.clear();
		m_cellSubscribers.clear();
		m_actorVisibleUsers.clear();
		m_userVisibleActors.clear();
		m_entityPositions.clear();
		m_userPositions.clear();
		m_dirtyUsers.clear();
		m_dirtyActors.clear();
		m_pendingVisibility.clear();
		m_losCache.clear();
		m_visibleMembership.clear();
		m_dirtyUserDedup.clear();
		m_dirtyActorDedup.clear();
		m_pendingVisibilityDedup.clear();

		RefreshContext();
	}

	void ServerAoiSystem::Tick()
	{
		RefreshContext();

		CollectDirtyUsersFromControlledActors();
		CollectDirtyActorsFromPhysics();

		UpdateDirtyUserSubscriptions();
		UpdateDirtyActorMembership();
		ResolvePendingVisibility();
	}

	void ServerAoiSystem::OnUserEnter(uint64 userId)
	{
		if (userId == 0)
			return;

		if (m_states.contains(userId)
			|| m_userStates.contains(userId)
			|| m_userVisibleActors.contains(userId)
			|| std::ranges::any_of(m_pendingVisibility, [userId](const AoiPendingVisibility& pending) { return pending.userId == userId; }))
		{
			OnUserLeave(userId);
		}

		auto& state = m_states[userId];
		state.visible.clear();
		state.entered.clear();
		state.left.clear();

		m_userStates.erase(userId);
		m_userPositions.erase(userId);
		MarkUserDirty(userId);
	}

	void ServerAoiSystem::OnUserLeave(uint64 userId)
	{
		if (userId == 0)
			return;

		if (auto it = m_userStates.find(userId); it != m_userStates.end())
		{
			for (const AoiCellCoord& cell : it->second.interestCells)
				RemoveSubscriberFromCell(cell, userId);
			m_userStates.erase(it);
		}

		m_userPositions.erase(userId);
		m_states.erase(userId);
		m_dirtyUserDedup.erase(userId);
		if (auto it = m_userVisibleActors.find(userId); it != m_userVisibleActors.end())
		{
			for (const AoiVisibleActorSlot& slot : it->second)
			{
				if (!slot.alive)
					continue;

				const AoiVisibilityKey key = MakeVisibilityKey(userId, slot.actor);
				auto membershipIt = m_visibleMembership.find(key);
				if (membershipIt == m_visibleMembership.end())
					continue;

				auto actorIt = m_actorVisibleUsers.find(slot.actor);
				if (actorIt != m_actorVisibleUsers.end()
					&& membershipIt->second.actorUserIndex < actorIt->second.size())
				{
					AoiVisibleUserSlot& actorSlot = actorIt->second[membershipIt->second.actorUserIndex];
					if (actorSlot.alive && actorSlot.userId == userId)
					{
						actorSlot.alive = false;
					}
				}

				m_visibleMembership.erase(membershipIt);
				CompactVisibleUsersIfNeeded(slot.actor);

				if (actorIt != m_actorVisibleUsers.end()
					&& std::ranges::none_of(actorIt->second, [](const AoiVisibleUserSlot& visibleSlot) { return visibleSlot.alive; }))
					m_actorVisibleUsers.erase(actorIt);
			}

			m_userVisibleActors.erase(it);
		}
		std::erase_if(m_losCache, [userId](const auto& kv)
		{
			return kv.first.userId == userId;
		});
		std::erase_if(m_visibleMembership, [userId](const auto& kv)
		{
			return kv.first.userId == userId;
		});
		std::erase_if(m_pendingVisibility, [userId](const AoiPendingVisibility& pending)
		{
			return pending.userId == userId;
		});
		std::erase_if(m_pendingVisibilityDedup, [userId](const AoiVisibilityKey& key)
		{
			return key.userId == userId;
		});
	}

	void ServerAoiSystem::OnActorSpawned(entt::entity actor)
	{
		if (actor == entt::null || !m_world.valid(actor))
			return;

		m_actorStates.erase(actor);
		MarkActorDirty(actor);
	}

	void ServerAoiSystem::OnActorDestroyed(entt::entity actor)
	{
		auto stIt = m_actorStates.find(actor);
		if (stIt != m_actorStates.end() && stIt->second.initialized)
			RemoveActorFromCell(stIt->second.anchorCell, actor);

		std::vector<uint64> visibleUsers;
		if (auto it = m_actorVisibleUsers.find(actor); it != m_actorVisibleUsers.end())
		{
			visibleUsers.reserve(it->second.size());
			for (const AoiVisibleUserSlot& slot : it->second)
			{
				if (!slot.alive)
					continue;

				visibleUsers.push_back(slot.userId);
				const AoiVisibilityKey key = MakeVisibilityKey(slot.userId, actor);
				auto membershipIt = m_visibleMembership.find(key);
				if (membershipIt == m_visibleMembership.end())
					continue;

			if (auto userIt = m_userVisibleActors.find(slot.userId); userIt != m_userVisibleActors.end()
				&& membershipIt->second.userActorIndex < userIt->second.size())
			{
				AoiVisibleActorSlot& actorSlot = userIt->second[membershipIt->second.userActorIndex];
					if (actorSlot.alive && actorSlot.actor == actor)
						actorSlot.alive = false;
				}

				m_visibleMembership.erase(membershipIt);
			}
			m_actorVisibleUsers.erase(it);
		}

		m_actorStates.erase(actor);
		m_entityPositions.erase(actor);
		m_dirtyActorDedup.erase(actor);
		std::erase_if(m_losCache, [actor](const auto& kv)
		{
			return kv.first.actor == actor;
		});
		std::erase_if(m_visibleMembership, [actor](const auto& kv)
		{
			return kv.first.actor == actor;
		});
		std::erase_if(m_pendingVisibility, [actor](const AoiPendingVisibility& pending)
		{
			return pending.actor == actor;
		});
		std::erase_if(m_pendingVisibilityDedup, [actor](const AoiVisibilityKey& key)
		{
			return key.actor == actor;
		});

		if (!m_world.valid(actor) || !m_world.all_of<NetId>(actor))
			return;

		const NetId netId = m_world.get<NetId>(actor);
		for (uint64 userId : visibleUsers)
		{
			if (auto stateIt = m_states.find(userId); stateIt != m_states.end())
			{
				if (stateIt->second.visible.erase(netId) > 0)
					stateIt->second.left.push_back(netId);
			}

			if (auto userIt = m_userVisibleActors.find(userId); userIt != m_userVisibleActors.end())
			{
				CompactVisibleActorsIfNeeded(userId);
				if (std::ranges::none_of(userIt->second, [](const AoiVisibleActorSlot& slot) { return slot.alive; }))
					m_userVisibleActors.erase(userIt);
			}
		}
	}

	bool ServerAoiSystem::IsVisible(uint64 userId, NetId netId) const
	{
		if (m_alwaysVisible.contains(netId))
			return true;

		if (auto it = m_states.find(userId); it != m_states.end())
			return it->second.visible.contains(netId);
		return false;
	}

	const UserAoiState* ServerAoiSystem::GetState(uint64 userId) const
	{
		if (auto it = m_states.find(userId); it != m_states.end())
			return &it->second;
		return nullptr;
	}

	const std::vector<AoiVisibleActorSlot>* ServerAoiSystem::GetVisibleActors(uint64 userId) const
	{
		if (auto it = m_userVisibleActors.find(userId); it != m_userVisibleActors.end())
			return &it->second;
		return nullptr;
	}

	void ServerAoiSystem::SetAlwaysVisible(NetId netId, bool always)
	{
		if (always)
			m_alwaysVisible.insert(netId);
		else
			m_alwaysVisible.erase(netId);
	}

	void ServerAoiSystem::RefreshContext()
	{
		if (auto* nwPtr = m_world.ctx().find<ServerNetWorld*>(); nwPtr)
			m_netWorld = *nwPtr;
		if (auto* phys = m_world.ctx().find<ServerPhysicsSystem>())
			m_serverPhysics = phys;
	}

	void ServerAoiSystem::CollectDirtyUsersFromControlledActors()
	{
		if (!m_netWorld)
			return;

		for (const auto& userId : m_states | std::views::keys)
		{
			const px::Vec3 newPos = ResolveUserPosition(userId);
			auto it = m_userPositions.find(userId);
			if (it == m_userPositions.end() || it->second != newPos)
			{
				m_userPositions[userId] = newPos;
				MarkUserDirty(userId);
			}
		}
	}

	void ServerAoiSystem::CollectDirtyActorsFromPhysics()
	{
		if (!m_serverPhysics)
			return;

		for (entt::entity actor : m_serverPhysics->GetLastActiveEntities())
			MarkActorDirty(actor);
	}

	void ServerAoiSystem::UpdateDirtyUserSubscriptions()
	{
		for (uint64 userId : m_dirtyUsers)
		{
			if (!m_states.contains(userId))
				continue;

			UserAoiState& state = m_states[userId];
			state.entered.clear();
			state.left.clear();

			const px::Vec3 userPos = ResolveUserPosition(userId);
			m_userPositions[userId] = userPos;
			UpdateUserAnchorAndInterestCells(userId, userPos);
		}

		m_dirtyUsers.clear();
		m_dirtyUserDedup.clear();
	}

	void ServerAoiSystem::UpdateDirtyActorMembership()
	{
		for (entt::entity actor : m_dirtyActors)
		{
			if (actor == entt::null || !m_world.valid(actor))
				continue;

			const px::Vec3 actorPos = ResolveActorPosition(actor);
			m_entityPositions[actor] = actorPos;
			UpdateActorAnchorCell(actor, actorPos);
		}

		m_dirtyActors.clear();
		m_dirtyActorDedup.clear();
	}
	
	void ServerAoiSystem::ResolvePendingVisibility()
	{
		std::erase_if(m_pendingVisibility, [this](const AoiPendingVisibility& pending)
		{
			if (pending.userId == 0 || pending.actor == entt::null || !m_states.contains(pending.userId))
				return true;
			if (!m_world.valid(pending.actor) || !m_world.all_of<NetId>(pending.actor))
				return true;
			return false;
		});

		for (const AoiPendingVisibility& pending : m_pendingVisibility)
			EvaluateVisibility(pending.userId, pending.actor);

		m_pendingVisibility.clear();
		m_pendingVisibilityDedup.clear();
	}

	void ServerAoiSystem::UpdateUserAnchorAndInterestCells(uint64 userId, const px::Vec3& userPos)
	{
		auto& userState = m_userStates[userId];

		const AoiCellCoord newAnchor = WorldToCell(userPos);
		if (!userState.initialized)
		{
			userState.initialized = true;
			userState.lastPos	  = userPos;
			userState.anchorCell  = newAnchor;

			std::vector<AoiCellCoord> oldCells;
			std::vector<AoiCellCoord> newCells = BuildInterestCells(userPos);
			userState.interestCells = newCells;
			OnUserCellsChanged(userId, oldCells, userState.interestCells);
			return;
		}

		if (!ShouldMoveAnchorCell(userPos, userState.anchorCell))
		{
			userState.lastPos = userPos;
			return;
		}

		std::vector<AoiCellCoord> oldCells = userState.interestCells;
		userState.lastPos		= userPos;
		userState.anchorCell	= newAnchor;
		userState.interestCells = BuildInterestCells(userPos);

		OnUserCellsChanged(userId, oldCells, userState.interestCells);
	}

	void ServerAoiSystem::UpdateActorAnchorCell(entt::entity actor, const px::Vec3& actorPos)
	{
		auto& actorState = m_actorStates[actor];
		const AoiCellCoord newAnchor = WorldToCell(actorPos);

		if (!actorState.initialized)
		{
			actorState.initialized = true;
			actorState.lastPos	   = actorPos;
			actorState.anchorCell  = newAnchor;
			AddActorToCell(newAnchor, actor);

			const uint64 cellKey = MakeCellKey(newAnchor);
			if (auto it = m_cellSubscribers.find(cellKey); it != m_cellSubscribers.end())
			{
				for (const AoiSubscriberSlot& slot : it->second)
					if (slot.alive)
						EnqueueVisibilityEval(slot.userId, actor);
			}
			return;
		}

		if (!ShouldMoveAnchorCell(actorPos, actorState.anchorCell))
		{
			actorState.lastPos = actorPos;
			return;
		}

		const AoiCellCoord oldAnchor = actorState.anchorCell;
		actorState.lastPos    = actorPos;
		actorState.anchorCell = newAnchor;

		OnActorCellChanged(actor, oldAnchor, newAnchor);
	}

	void ServerAoiSystem::OnUserCellsChanged(uint64 userId, std::span<const AoiCellCoord> oldCells, std::span<const AoiCellCoord> newCells)
	{
		std::unordered_set<AoiCellCoord, AoiCellCoordHash> newCellSet;
		newCellSet.reserve(newCells.size());
		for (const AoiCellCoord& cell : newCells)
			newCellSet.insert(cell);

		for (const AoiCellCoord& oldCell : oldCells)
		{
			if (newCellSet.contains(oldCell))
				continue;

			RemoveSubscriberFromCell(oldCell, userId);

			const uint64 cellKey = MakeCellKey(oldCell);
			if (auto it = m_cellActors.find(cellKey); it != m_cellActors.end())
			{
				for (const AoiActorSlot& slot : it->second)
					if (slot.alive)
						EnqueueVisibilityEval(userId, slot.actor);
			}
		}

		std::unordered_set<AoiCellCoord, AoiCellCoordHash> oldCellSet;
		oldCellSet.reserve(oldCells.size());
		for (const AoiCellCoord& cell : oldCells)
			oldCellSet.insert(cell);

		for (const AoiCellCoord& newCell : newCells)
		{
			if (oldCellSet.contains(newCell))
				continue;

			AddSubscriberToCell(newCell, userId);

			const uint64 cellKey = MakeCellKey(newCell);
			if (auto it = m_cellActors.find(cellKey); it != m_cellActors.end())
			{
				for (const AoiActorSlot& slot : it->second)
					if (slot.alive)
						EnqueueVisibilityEval(userId, slot.actor);
			}
		}
	}

	void ServerAoiSystem::OnActorCellChanged(entt::entity actor, const AoiCellCoord& oldCell, const AoiCellCoord& newCell)
	{
		if (oldCell == newCell)
			return;

		RemoveActorFromCell(oldCell, actor);
		AddActorToCell(newCell, actor);

		std::unordered_set<uint64> affectedUsers;
		affectedUsers.reserve(32);

		const auto collectSubscribers = [&](const AoiCellCoord& cell)
		{
			const uint64 cellKey = MakeCellKey(cell);
			if (auto it = m_cellSubscribers.find(cellKey); it != m_cellSubscribers.end())
			{
				for (const AoiSubscriberSlot& slot : it->second)
				{
					if (!slot.alive)
						continue;
					affectedUsers.insert(slot.userId);
				}
			}
		};

		collectSubscribers(oldCell);
		collectSubscribers(newCell);

		for (uint64 userId : affectedUsers)
			EnqueueVisibilityEval(userId, actor);
	}

	void ServerAoiSystem::EvaluateVisibility(uint64 userId, entt::entity actor)
	{
		if (userId == 0 || actor == entt::null || !m_world.valid(actor) || !m_world.all_of<NetId>(actor))
			return;

		auto stateIt = m_states.find(userId);
		if (stateIt == m_states.end())
			return;

		UserAoiState& state = stateIt->second;
		const NetId netId = m_world.get<NetId>(actor);
		auto& visibleUsers  = m_actorVisibleUsers[actor];
		auto& visibleActors = m_userVisibleActors[userId];
		const AoiVisibilityKey visibilityKey = MakeVisibilityKey(userId, actor);

		const auto addVisibleUser = [&](uint64 uid)
		{
			visibleUsers.push_back(AoiVisibleUserSlot{ uid, true });
		};
		const auto addVisibleActor = [&](entt::entity e)
		{
			visibleActors.push_back(AoiVisibleActorSlot{ e, true });
		};

		if (m_alwaysVisible.contains(netId))
		{
			if (!m_visibleMembership.contains(visibilityKey))
			{
				const size_t actorUserIndex = visibleUsers.size();
				const size_t userActorIndex = visibleActors.size();
				addVisibleUser(userId);
				addVisibleActor(actor);
				m_visibleMembership.emplace(visibilityKey, AoiVisibleMembershipEntry{ actorUserIndex, userActorIndex });
			}
			if (state.visible.insert(netId).second)
			{
				state.entered.push_back(netId);
			}
			return;
		}

		const px::Vec3 userPos    = ResolveUserPosition(userId);
		const px::Vec3 actorPos   = ResolveActorPosition(actor);
		const bool	   wasVisible = state.visible.contains(netId);
		const bool	   nowVisible = PassesVisibilityTests(userId, actor, userPos, actorPos, wasVisible);

		if (nowVisible)
		{
			if (!m_visibleMembership.contains(visibilityKey))
			{
				const size_t actorUserIndex = visibleUsers.size();
				const size_t userActorIndex = visibleActors.size();
				addVisibleUser(userId);
				addVisibleActor(actor);
				m_visibleMembership.emplace(visibilityKey, AoiVisibleMembershipEntry{ actorUserIndex, userActorIndex });
			}
			if (state.visible.insert(netId).second)
			{
				state.entered.push_back(netId);
			}
		}
		else
		{
			const bool erasedVisible = state.visible.erase(netId) > 0;
			if (erasedVisible)
				state.left.push_back(netId);

			if (erasedVisible || m_visibleMembership.contains(visibilityKey))
			{
				if (auto membershipIt = m_visibleMembership.find(visibilityKey); membershipIt != m_visibleMembership.end())
				{
					if (membershipIt->second.actorUserIndex < visibleUsers.size())
					{
						AoiVisibleUserSlot& slot = visibleUsers[membershipIt->second.actorUserIndex];
						if (slot.alive && slot.userId == userId)
							slot.alive = false;
					}

					if (membershipIt->second.userActorIndex < visibleActors.size())
					{
						AoiVisibleActorSlot& slot = visibleActors[membershipIt->second.userActorIndex];
						if (slot.alive && slot.actor == actor)
							slot.alive = false;
					}

					m_visibleMembership.erase(membershipIt);
				}
			}
		}

		CompactVisibleUsersIfNeeded(actor);
		CompactVisibleActorsIfNeeded(userId);
		if (std::ranges::none_of(visibleUsers, [](const AoiVisibleUserSlot& slot) { return slot.alive; }))
			m_actorVisibleUsers.erase(actor);
		if (std::ranges::none_of(visibleActors, [](const AoiVisibleActorSlot& slot) { return slot.alive; }))
			m_userVisibleActors.erase(userId);
	}

	bool ServerAoiSystem::PassesVisibilityTests(uint64 userId, entt::entity actor, const px::Vec3& userPos, const px::Vec3& actorPos, bool wasVisible)
	{
		//JAM_UNUSED(userId);
		//JAM_UNUSED(actor);

		const float bias = wasVisible ? m_cfg.hysteresisOffset : 0.0f;
		const px::Vec3 d = actorPos - userPos;

		bool inRange = false;
		switch (m_cfg.condition)
		{
		case eAoiCondition::Circle:
			inRange = (d.x * d.x + d.z * d.z) <= ((m_cfg.radius + bias) * (m_cfg.radius + bias));
			break;

		case eAoiCondition::Sphere:
			inRange = d.MagnitudeSquared() <= ((m_cfg.radius + bias) * (m_cfg.radius + bias));
			break;

		case eAoiCondition::AABB_2D:
			inRange = std::abs(d.x) <= (m_cfg.aabbX + bias) && std::abs(d.z) <= (m_cfg.aabbZ + bias);
			break;

		case eAoiCondition::AABB_3D:
			inRange = std::abs(d.x) <= (m_cfg.aabbX + bias) && std::abs(d.y) <= (m_cfg.aabbY + bias) && std::abs(d.z) <= (m_cfg.aabbZ + bias);
			break;
		}

		if (!inRange)
			return false;

		if (!m_cfg.enableLos)
			return true;

		return PassesLos(userId, actor, userPos, actorPos, wasVisible);
	}

	bool ServerAoiSystem::PassesLos(uint64 userId, entt::entity actor, const px::Vec3& userPos, const px::Vec3& actorPos, bool wasVisible)
	{
		if (!m_physics)
			return true;

		const uint32 currentTick = m_world.ctx().contains<TickCounter>() ? m_world.ctx().get<TickCounter>().tick : 0;
		const AoiVisibilityKey key = MakeVisibilityKey(userId, actor);

		if (wasVisible)
		{
			if (auto it = m_losCache.find(key); it != m_losCache.end())
			{
				const uint32 age = (currentTick >= it->second.lastTick) ? (currentTick - it->second.lastTick) : 0;
				if (age < std::max(1u, m_cfg.losRetestTicks))
					return it->second.lastVisible;
			}
		}

		const px::Vec3 eyeOffset{ 0.0f, m_cfg.losEyeOffset, 0.0f };
		const bool visible = m_physics->RaycastLOS(userPos + eyeOffset, actorPos + eyeOffset);
		m_losCache[key] = AoiLosCacheEntry{ .lastTick = currentTick, .lastVisible = visible };
		return visible;
	}

	px::Vec3 ServerAoiSystem::ResolveActorPosition(entt::entity actor) const
	{
		if (const auto* cs = m_world.try_get<CharAuthorityState>(actor))
			return cs->state.pos;
		if (const auto* rs = m_world.try_get<RigidAuthorityState>(actor))
			return rs->state.pose.p;
		return {};
	}

	px::Vec3 ServerAoiSystem::ResolveUserPosition(uint64 userId) const
	{
		if (!m_netWorld)
			return {};

		const entt::entity actor = m_netWorld->GetControlledEntity(userId);
		if (actor == entt::null || !m_world.valid(actor))
			return {};
		return ResolveActorPosition(actor);
	}

	AoiCellCoord ServerAoiSystem::WorldToCell(const px::Vec3& pos) const
	{
		const float cellSize = std::max(1.0f, m_cfg.gridCellSize);
		return AoiCellCoord
		{
			.x = static_cast<int32>(std::floor(pos.x / cellSize)),
			.z = static_cast<int32>(std::floor(pos.z / cellSize))
		};
	}

	bool ServerAoiSystem::ShouldMoveAnchorCell(const px::Vec3& pos, const AoiCellCoord& currentCell) const
	{
		const float cellSize = std::max(1.0f, m_cfg.gridCellSize);
		const float minX	 = (static_cast<float>(currentCell.x)	  * cellSize) - m_cfg.cellHysteresisOffset;
		const float maxX	 = (static_cast<float>(currentCell.x + 1) * cellSize) + m_cfg.cellHysteresisOffset;
		const float minZ	 = (static_cast<float>(currentCell.z)     * cellSize) - m_cfg.cellHysteresisOffset;
		const float maxZ	 = (static_cast<float>(currentCell.z + 1) * cellSize) + m_cfg.cellHysteresisOffset;

		return pos.x < minX || pos.x >= maxX || pos.z < minZ || pos.z >= maxZ;
	}

	AoiCellRect ServerAoiSystem::BuildInterestRect(const px::Vec3& origin, float extraBias) const
	{
		const float cellSize = std::max(1.0f, m_cfg.gridCellSize);

		float hx = m_cfg.radius;
		float hz = m_cfg.radius;
		if (m_cfg.condition == eAoiCondition::AABB_2D || m_cfg.condition == eAoiCondition::AABB_3D)
		{
			hx = m_cfg.aabbX;
			hz = m_cfg.aabbZ;
		}

		hx += m_cfg.hysteresisOffset + extraBias;
		hz += m_cfg.hysteresisOffset + extraBias;

		return AoiCellRect
		{
			.minX = static_cast<int32>(std::floor((origin.x - hx) / cellSize)),
			.maxX = static_cast<int32>(std::floor((origin.x + hx) / cellSize)),
			.minZ = static_cast<int32>(std::floor((origin.z - hz) / cellSize)),
			.maxZ = static_cast<int32>(std::floor((origin.z + hz) / cellSize))
		};
	}

	std::vector<AoiCellCoord> ServerAoiSystem::BuildInterestCells(const px::Vec3& origin) const
	{
		std::vector<AoiCellCoord> out;
		const AoiCellRect rect = BuildInterestRect(origin);
		if (rect.IsEmpty())
			return out;

		out.reserve(static_cast<size_t>((rect.maxX - rect.minX + 1) * (rect.maxZ - rect.minZ + 1)));
		for (int32 x = rect.minX; x <= rect.maxX; ++x)
		{
			for (int32 z = rect.minZ; z <= rect.maxZ; ++z)
				out.push_back(AoiCellCoord{ x, z });
		}
		return out;
	}

	uint64 ServerAoiSystem::MakeCellKey(const AoiCellCoord& cell) const
	{
		return (static_cast<uint64>(static_cast<uint32>(cell.x)) << 32)
			| static_cast<uint32>(cell.z);
	}

	void ServerAoiSystem::AddActorToCell(const AoiCellCoord& cell, entt::entity actor)
	{
		auto& slots = m_cellActors[MakeCellKey(cell)];
		slots.push_back(AoiActorSlot{ actor, true });
	}

	void ServerAoiSystem::RemoveActorFromCell(const AoiCellCoord& cell, entt::entity actor)
	{
		const uint64 cellKey = MakeCellKey(cell);
		auto it = m_cellActors.find(cellKey);
		if (it == m_cellActors.end())
			return;

		for (AoiActorSlot& slot : it->second)
		{
			if (slot.alive && slot.actor == actor)
			{
				slot.alive = false;
				break;
			}
		}

		CompactCellActorsIfNeeded(cellKey);
	}

	void ServerAoiSystem::AddSubscriberToCell(const AoiCellCoord& cell, uint64 userId)
	{
		auto& slots = m_cellSubscribers[MakeCellKey(cell)];
		slots.push_back(AoiSubscriberSlot{ userId, true });
	}

	void ServerAoiSystem::RemoveSubscriberFromCell(const AoiCellCoord& cell, uint64 userId)
	{
		const uint64 cellKey = MakeCellKey(cell);
		auto it = m_cellSubscribers.find(cellKey);
		if (it == m_cellSubscribers.end())
			return;

		for (AoiSubscriberSlot& slot : it->second)
		{
			if (slot.alive && slot.userId == userId)
			{
				slot.alive = false;
				break;
			}
		}

		CompactCellSubscribersIfNeeded(cellKey);
	}

	void ServerAoiSystem::EnqueueVisibilityEval(uint64 userId, entt::entity actor)
	{
		const AoiVisibilityKey key = MakeVisibilityKey(userId, actor);
		if (!m_pendingVisibilityDedup.insert(key).second)
			return;

		m_pendingVisibility.push_back(AoiPendingVisibility{ userId, actor });
	}

	AoiVisibilityKey ServerAoiSystem::MakeVisibilityKey(uint64 userId, entt::entity actor) const
	{
		return AoiVisibilityKey{ .userId = userId, .actor = actor };
	}

	void ServerAoiSystem::MarkUserDirty(uint64 userId)
	{
		if (userId == 0 || !m_dirtyUserDedup.insert(userId).second)
			return;
		m_dirtyUsers.push_back(userId);
	}

	void ServerAoiSystem::MarkActorDirty(entt::entity actor)
	{
		if (actor == entt::null || !m_dirtyActorDedup.insert(actor).second)
			return;
		m_dirtyActors.push_back(actor);
	}

	void ServerAoiSystem::CompactCellActorsIfNeeded(uint64 cellKey)
	{
		auto it = m_cellActors.find(cellKey);
		if (it == m_cellActors.end())
			return;

		auto& slots = it->second;
		size_t dead = 0;
		for (const AoiActorSlot& slot : slots)
			if (!slot.alive)
				++dead;

		if (dead < m_cfg.compactMinDeadCount)
			return;
		if (slots.empty() || (static_cast<float>(dead) / static_cast<float>(slots.size())) < m_cfg.compactDeadRatio)
			return;

		std::erase_if(slots, [](const AoiActorSlot& slot) { return !slot.alive; });
	}

	void ServerAoiSystem::CompactCellSubscribersIfNeeded(uint64 cellKey)
	{
		auto it = m_cellSubscribers.find(cellKey);
		if (it == m_cellSubscribers.end())
			return;

		auto& slots = it->second;
		size_t dead = 0;
		for (const AoiSubscriberSlot& slot : slots)
			if (!slot.alive)
				++dead;

		if (dead < m_cfg.compactMinDeadCount)
			return;
		if (slots.empty() || (static_cast<float>(dead) / static_cast<float>(slots.size())) < m_cfg.compactDeadRatio)
			return;

		std::erase_if(slots, [](const AoiSubscriberSlot& slot) { return !slot.alive; });
	}

	void ServerAoiSystem::CompactVisibleUsersIfNeeded(entt::entity actor)
	{
		auto it = m_actorVisibleUsers.find(actor);
		if (it == m_actorVisibleUsers.end())
			return;

		auto& slots = it->second;
		size_t dead = 0;
		for (const AoiVisibleUserSlot& slot : slots)
		{
			if (!slot.alive)
				++dead;
		}

		if (dead < m_cfg.compactMinDeadCount)
			return;
		if (slots.empty() || (static_cast<float>(dead) / static_cast<float>(slots.size())) < m_cfg.compactDeadRatio)
			return;

		std::erase_if(slots, [](const AoiVisibleUserSlot& slot) { return !slot.alive; });
		for (size_t i = 0; i < slots.size(); ++i)
		{
			const AoiVisibilityKey key = MakeVisibilityKey(slots[i].userId, actor);
			if (auto membershipIt = m_visibleMembership.find(key); membershipIt != m_visibleMembership.end())
				membershipIt->second.actorUserIndex = i;
		}
	}

	void ServerAoiSystem::CompactVisibleActorsIfNeeded(uint64 userId)
	{
		auto it = m_userVisibleActors.find(userId);
		if (it == m_userVisibleActors.end())
			return;

		auto& slots = it->second;
		size_t dead = 0;
		for (const AoiVisibleActorSlot& slot : slots)
		{
			if (!slot.alive)
				++dead;
		}

		if (dead < m_cfg.compactMinDeadCount)
			return;
		if (slots.empty() || (static_cast<float>(dead) / static_cast<float>(slots.size())) < m_cfg.compactDeadRatio)
			return;

		std::erase_if(slots, [](const AoiVisibleActorSlot& slot) { return !slot.alive; });
		for (size_t i = 0; i < slots.size(); ++i)
		{
			const AoiVisibilityKey key = MakeVisibilityKey(userId, slots[i].actor);
			if (auto membershipIt = m_visibleMembership.find(key); membershipIt != m_visibleMembership.end())
				membershipIt->second.userActorIndex = i;
		}
	}
}
