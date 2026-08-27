#include "pch.h"
#include "jamnet/runtime/world/simulation/server/ServerAoiSystem.h"
#include "jamnet/runtime/world/simulation/server/WorldMetrics.h"

#include <jampx/PhysicsFacade.h>

#include "jamnet/runtime/world/simulation/common/WorldContext.h"
#include "jamnet/runtime/world/simulation/server/ServerWorld.h"
#include "jamnet/runtime/world/simulation/server/ServerPhysicsSystem.h"

namespace jam::net
{
	ServerAoiSystem::ServerAoiSystem(
		entt::registry& world, 
		px::PhysicsFacade* physics, 
		WorldMetrics& metrics,
		ServerWorld& serverWorld, 
		ServerPhysicsSystem& serverPhysics, 
		TickCounter& tickCounter)
		: m_registry(world), 
		m_physics(physics), 
		m_world(serverWorld), 
		m_serverPhysics(serverPhysics),
		m_tickCounter(tickCounter), 
		m_metrics(metrics)
	{
	}

	void ServerAoiSystem::Init(const AoiConfig& cfg)
	{
		m_cfg = cfg;

		m_states.clear();
		m_actorStates.clear();
		m_userStates.clear();
		m_cellActors.clear();
		m_cellSubscribers.clear();
		m_actorVisibleUsers.clear();
		m_userVisibleActors.clear();
		m_entityPositions.clear();
		m_dirtyUsers.clear();
		m_dirtyActors.clear();
		m_pendingVisibility.clear();
		m_losCache.clear();
		m_visibleMembership.clear();
		m_losActorsByUser.clear();
		m_losUsersByActor.clear();
		m_userGenerations.clear();
		m_dirtyUserDedup.clear();
		m_dirtyActorDedup.clear();
		m_pendingVisibilityDedup.clear();
		m_pendingCellActorCompactions.clear();
		m_pendingCellSubscriberCompactions.clear();

	}

	void ServerAoiSystem::Tick()
	{
		ClearTransientEvents();

		CollectDirtyActorsFromPhysics();

		UpdateDirtyUserSubscriptions();
		UpdateDirtyActorMembership();
		ResolvePendingVisibility();
		FlushPendingCompactions();
	}

	void ServerAoiSystem::OnUserEnter(uint64 userId)
	{
		if (userId == 0)
			return;

		if (m_states.contains(userId)
			|| m_userStates.contains(userId)
			|| m_userVisibleActors.contains(userId))
		{
			OnUserLeave(userId);
		}
		++m_userGenerations[userId];

		auto& state = m_states[userId];
		state.visible.clear();
		state.entered.clear();
		state.left.clear();

		m_userStates.erase(userId);
		MarkUserDirty(userId);
	}

	void ServerAoiSystem::OnUserLeave(uint64 userId)
	{
		if (userId == 0)
			return;
		++m_userGenerations[userId];

		if (auto it = m_userStates.find(userId); it != m_userStates.end())
		{
			for (const AoiCellCoord& cell : it->second.interestCells)
				RemoveSubscriberFromCell(cell, userId);
			m_userStates.erase(it);
		}

		m_states.erase(userId);
		m_dirtyUserDedup.erase(userId);
		while (true)
		{
			auto it = m_userVisibleActors.find(userId);
			if (it == m_userVisibleActors.end())
				break;
			JAM_ASSERT(!it->second.empty());
			if (it->second.empty())
			{
				m_userVisibleActors.erase(it);
				break;
			}
			if (!RemoveVisibleMembership(userId, it->second.back().actor))
				break;
		}
		RemoveLosForUser(userId);
	}

	void ServerAoiSystem::OnControlledActorChanged(uint64 userId)
	{
		if (userId != 0 && m_states.contains(userId))
			MarkUserDirty(userId);
	}

	bool ServerAoiSystem::IsUserReady(uint64 userId) const
	{
		const auto it = m_userStates.find(userId);
		return it != m_userStates.end() && it->second.initialized;
	}

	bool ServerAoiSystem::IsActorRegistered(entt::entity actor) const
	{
		const auto it = m_actorStates.find(actor);
		return it != m_actorStates.end() && it->second.initialized;
	}

	void ServerAoiSystem::OnActorSpawned(entt::entity actor)
	{
		if (actor == entt::null || !m_registry.valid(actor)
			|| m_registry.all_of<ReplicationDisabledTag>(actor))
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
		while (true)
		{
			auto it = m_actorVisibleUsers.find(actor);
			if (it == m_actorVisibleUsers.end())
				break;
			if (visibleUsers.empty())
				visibleUsers.reserve(it->second.size());
			JAM_ASSERT(!it->second.empty());
			if (it->second.empty())
			{
				m_actorVisibleUsers.erase(it);
				break;
			}

			const uint64 userId = it->second.back().userId;
			visibleUsers.push_back(userId);
			if (!RemoveVisibleMembership(userId, actor))
				break;
		}

		m_actorStates.erase(actor);
		m_entityPositions.erase(actor);
		m_dirtyActorDedup.erase(actor);
		RemoveLosForActor(actor);

		if (!m_registry.valid(actor) || !m_registry.all_of<ActorId>(actor))
			return;

		const ActorId actorId = m_registry.get<ActorId>(actor);
		for (uint64 userId : visibleUsers)
		{
			if (auto stateIt = m_states.find(userId); stateIt != m_states.end())
			{
				if (stateIt->second.visible.contains(actor))
				{
					stateIt->second.visible.erase(actor);
					stateIt->second.left.push_back(actorId);
					m_metrics.RecordAoiLeftActor();
				}
			}
		}
	}

	bool ServerAoiSystem::IsVisible(uint64 userId, ActorId actorId) const
	{
		const entt::entity actor = m_world.ResolveActor(actorId);
		if (actor != entt::null)
			if (auto it = m_states.find(userId); it != m_states.end())
				return it->second.visible.contains(actor);
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

	void ServerAoiSystem::ClearTransientEvents()
	{
		for (UserAoiState& state : m_states | std::views::values)
		{
			state.entered.clear();
			state.left.clear();
		}
	}

	void ServerAoiSystem::CollectDirtyActorsFromPhysics()
	{
		for (entt::entity actor : m_serverPhysics.GetLastActiveEntities())
		{
			MarkActorDirty(actor);

			if (actor == entt::null || !m_registry.valid(actor))
				continue;

			if (const auto* control = m_registry.try_get<ControlTag>(actor); control && control->userId != 0)
				MarkUserDirty(control->userId);
		}
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
			UpdateUserAnchorAndInterestCells(userId, userPos);
		}

		m_dirtyUsers.clear();
		m_dirtyUserDedup.clear();
	}

	void ServerAoiSystem::UpdateDirtyActorMembership()
	{
		for (entt::entity actor : m_dirtyActors)
		{
			if (actor == entt::null || !m_registry.valid(actor))
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
		for (const AoiPendingVisibility& pending : m_pendingVisibility)
		{
			if (pending.userId == 0 || pending.actor == entt::null || !m_states.contains(pending.userId))
				continue;
			if (!m_registry.valid(pending.actor) || !m_registry.all_of<ActorId>(pending.actor))
				continue;
			if (m_userGenerations[pending.userId] != pending.userGeneration)
			{
				MarkUserDirty(pending.userId);
				continue;
			}
			EvaluateVisibility(pending.userId, pending.actor);
		}

		for (const UserAoiState& state : m_states | std::views::values)
			m_metrics.RecordAoiNeighbors(state.visible.size());

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
		if (userState.lastPos == userPos)
			return;
		userState.travelX += std::abs(static_cast<double>(userPos.x) - static_cast<double>(userState.lastPos.x));
		userState.travelZ += std::abs(static_cast<double>(userPos.z) - static_cast<double>(userState.lastPos.z));

		if (newAnchor == userState.anchorCell)
		{
			userState.lastPos = userPos;
			EnqueueVisibilityForUserCells(userId, userState.interestCells);
			EnqueueVisibilityForVisibleActors(userId);
			return;
		}

		std::vector<AoiCellCoord> oldCells = userState.interestCells;
		userState.lastPos		= userPos;
		userState.anchorCell	= newAnchor;
		userState.interestCells = BuildInterestCells(userPos);

		OnUserCellsChanged(userId, oldCells, userState.interestCells);
		EnqueueVisibilityForUserCells(userId, userState.interestCells);
		EnqueueVisibilityForVisibleActors(userId);
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
		if (actorState.lastPos == actorPos)
			return;
		actorState.travelX += std::abs(static_cast<double>(actorPos.x) - static_cast<double>(actorState.lastPos.x));
		actorState.travelZ += std::abs(static_cast<double>(actorPos.z) - static_cast<double>(actorState.lastPos.z));

		if (newAnchor == actorState.anchorCell)
		{
			actorState.lastPos = actorPos;
			EnqueueVisibilityForActorCell(actor, actorState.anchorCell);
			EnqueueVisibilityForVisibleUsers(actor);
			return;
		}

		const AoiCellCoord oldAnchor = actorState.anchorCell;
		actorState.lastPos    = actorPos;
		actorState.anchorCell = newAnchor;

		OnActorCellChanged(actor, oldAnchor, newAnchor);
		EnqueueVisibilityForVisibleUsers(actor);
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

		EnqueueVisibilityForActorCell(actor, newCell);
	}

	void ServerAoiSystem::EnqueueVisibilityForUserCells(uint64 userId, std::span<const AoiCellCoord> cells)
	{
		for (const AoiCellCoord& cell : cells)
		{
			const uint64 cellKey = MakeCellKey(cell);
			if (auto it = m_cellActors.find(cellKey); it != m_cellActors.end())
			{
				for (const AoiActorSlot& slot : it->second)
					if (slot.alive)
						EnqueueVisibilityEval(userId, slot.actor);
			}
		}
	}

	void ServerAoiSystem::EnqueueVisibilityForActorCell(entt::entity actor, const AoiCellCoord& cell)
	{
		const uint64 cellKey = MakeCellKey(cell);
		if (auto it = m_cellSubscribers.find(cellKey); it != m_cellSubscribers.end())
		{
			for (const AoiSubscriberSlot& slot : it->second)
				if (slot.alive)
					EnqueueVisibilityEval(slot.userId, actor);
		}
	}

	void ServerAoiSystem::EnqueueVisibilityForVisibleActors(uint64 userId)
	{
		if (auto it = m_userVisibleActors.find(userId); it != m_userVisibleActors.end())
		{
			for (const AoiVisibleActorSlot& slot : it->second)
				EnqueueVisibilityEval(userId, slot.actor);
		}
	}

	void ServerAoiSystem::EnqueueVisibilityForVisibleUsers(entt::entity actor)
	{
		if (auto it = m_actorVisibleUsers.find(actor); it != m_actorVisibleUsers.end())
		{
			for (const AoiVisibleUserSlot& slot : it->second)
				EnqueueVisibilityEval(slot.userId, actor);
		}
	}

	void ServerAoiSystem::EvaluateVisibility(uint64 userId, entt::entity actor)
	{
		if (userId == 0 || actor == entt::null || !m_registry.valid(actor) || !m_registry.all_of<ActorId>(actor))
			return;

		auto stateIt = m_states.find(userId);
		if (stateIt == m_states.end())
			return;

		UserAoiState& state = stateIt->second;
		const ActorId actorId = m_registry.get<ActorId>(actor);
		const AoiVisibilityKey visibilityKey = MakeVisibilityKey(userId, actor);
		const bool wasVisible = state.visible.contains(actor);
		auto membershipIt = m_visibleMembership.find(visibilityKey);

		const AoiUserCellState* userCellState = nullptr;
		const AoiActorCellState* actorCellState = nullptr;
		if (wasVisible
			&& membershipIt != m_visibleMembership.end()
			&& membershipIt->second.certificateValid
			&& m_cfg.condition == eAoiCondition::AABB_2D
			&& !m_cfg.enableLos)
		{
			if (const auto userCellIt = m_userStates.find(userId); userCellIt != m_userStates.end())
				userCellState = &userCellIt->second;
			if (const auto actorCellIt = m_actorStates.find(actor); actorCellIt != m_actorStates.end())
				actorCellState = &actorCellIt->second;

			if (userCellState && actorCellState)
			{
				const AoiVisibleMembershipEntry& certificate = membershipIt->second;
				const double movementBoundX = std::max(0.0, userCellState->travelX - certificate.userTravelX)
					+ std::max(0.0, actorCellState->travelX - certificate.actorTravelX);
				const double movementBoundZ = std::max(0.0, userCellState->travelZ - certificate.userTravelZ)
					+ std::max(0.0, actorCellState->travelZ - certificate.actorTravelZ);
				const double leaveExtentX = static_cast<double>(m_cfg.aabbX + m_cfg.hysteresisOffset);
				const double leaveExtentZ = static_cast<double>(m_cfg.aabbZ + m_cfg.hysteresisOffset);
				constexpr double kCertificateSafetyMargin = 1.0e-4;
				if (certificate.lastAbsDx + movementBoundX + kCertificateSafetyMargin <= leaveExtentX
					&& certificate.lastAbsDz + movementBoundZ + kCertificateSafetyMargin <= leaveExtentZ)
				{
					return;
				}
			}
		}

		if (!userCellState)
			if (const auto it = m_userStates.find(userId); it != m_userStates.end())
				userCellState = &it->second;
		const px::Vec3 userPos = userCellState
			? userCellState->lastPos
			: ResolveUserPosition(userId);
		const auto actorPosIt = m_entityPositions.find(actor);
		const px::Vec3 actorPos = actorPosIt != m_entityPositions.end()
			? actorPosIt->second
			: ResolveActorPosition(actor);
		const bool nowVisible = PassesVisibilityTests(userId, actor, userPos, actorPos, wasVisible);

		const auto refreshCertificate = [&](AoiVisibleMembershipEntry& certificate)
		{
			if (!userCellState)
				if (const auto it = m_userStates.find(userId); it != m_userStates.end())
					userCellState = &it->second;
			if (!actorCellState)
				if (const auto it = m_actorStates.find(actor); it != m_actorStates.end())
					actorCellState = &it->second;

			if (m_cfg.condition != eAoiCondition::AABB_2D || m_cfg.enableLos || !userCellState || !actorCellState)
			{
				certificate.certificateValid = false;
				return;
			}

			certificate.lastAbsDx = std::abs(static_cast<double>(actorPos.x) - static_cast<double>(userPos.x));
			certificate.lastAbsDz = std::abs(static_cast<double>(actorPos.z) - static_cast<double>(userPos.z));
			certificate.userTravelX = userCellState->travelX;
			certificate.userTravelZ = userCellState->travelZ;
			certificate.actorTravelX = actorCellState->travelX;
			certificate.actorTravelZ = actorCellState->travelZ;
			certificate.certificateValid = true;
		};

		if (nowVisible)
		{
			if (membershipIt == m_visibleMembership.end())
			{
				auto& visibleUsers = m_actorVisibleUsers[actor];
				auto& visibleActors = m_userVisibleActors[userId];
				const size_t actorUserIndex = visibleUsers.size();
				const size_t userActorIndex = visibleActors.size();
				visibleUsers.push_back(AoiVisibleUserSlot{ userId });
				visibleActors.push_back(AoiVisibleActorSlot{ actor });
				const auto [insertedIt, inserted] = m_visibleMembership.emplace(
					visibilityKey,
					AoiVisibleMembershipEntry{
						.actorUserIndex = actorUserIndex,
						.userActorIndex = userActorIndex,
					});
				(void)inserted;
				membershipIt = insertedIt;
			}
			refreshCertificate(membershipIt->second);
			if (!wasVisible)
			{
				state.visible.push(actor);
				state.entered.push_back(actorId);
				m_metrics.RecordAoiEnteredActor();
			}
			return;
		}

		if (wasVisible)
		{
			state.visible.erase(actor);
			state.left.push_back(actorId);
			m_metrics.RecordAoiLeftActor();
		}

		if (membershipIt == m_visibleMembership.end())
			return;

		(void)RemoveVisibleMembership(userId, actor);
	}

	bool ServerAoiSystem::PassesVisibilityTests(uint64 userId, entt::entity actor, const px::Vec3& userPos, const px::Vec3& actorPos, bool wasVisible)
	{
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

		const uint32 currentTick = m_tickCounter.tick;
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
		const auto [it, inserted] = m_losCache.insert_or_assign(key, AoiLosCacheEntry{ .lastTick = currentTick, .lastVisible = visible });
		(void)it;
		if (inserted)
		{
			m_losActorsByUser[userId].insert(actor);
			m_losUsersByActor[actor].insert(userId);
		}
		return visible;
	}

	px::Vec3 ServerAoiSystem::ResolveActorPosition(entt::entity actor) const
	{
		if (const auto* cs = m_registry.try_get<CharAuthorityState>(actor))
			return cs->state.pos;
		if (const auto* rs = m_registry.try_get<RigidAuthorityState>(actor))
			return rs->state.pose.p;
		return {};
	}

	px::Vec3 ServerAoiSystem::ResolveUserPosition(uint64 userId) const
	{
		const entt::entity actor = m_world.GetControlledEntity(userId);
		if (actor == entt::null || !m_registry.valid(actor))
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

	AoiCellRect ServerAoiSystem::BuildInterestRect(const px::Vec3& origin) const
	{
		const float cellSize = std::max(1.0f, m_cfg.gridCellSize);

		float hx = m_cfg.radius;
		float hz = m_cfg.radius;
		if (m_cfg.condition == eAoiCondition::AABB_2D || m_cfg.condition == eAoiCondition::AABB_3D)
		{
			hx = m_cfg.aabbX;
			hz = m_cfg.aabbZ;
		}

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

		m_pendingCellActorCompactions.insert(cellKey);
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

		m_pendingCellSubscriberCompactions.insert(cellKey);
	}

	void ServerAoiSystem::EnqueueVisibilityEval(uint64 userId, entt::entity actor)
	{
		const AoiVisibilityKey key = MakeVisibilityKey(userId, actor);
		if (!m_pendingVisibilityDedup.insert(key).second)
			return;

		m_pendingVisibility.push_back(AoiPendingVisibility{
			.userId = userId,
			.actor = actor,
			.userGeneration = m_userGenerations[userId],
		});
	}

	void ServerAoiSystem::RemoveLosForUser(uint64 userId)
	{
		auto actorsIt = m_losActorsByUser.find(userId);
		if (actorsIt == m_losActorsByUser.end())
			return;

		for (entt::entity actor : actorsIt->second)
		{
			m_losCache.erase(MakeVisibilityKey(userId, actor));
			if (auto usersIt = m_losUsersByActor.find(actor); usersIt != m_losUsersByActor.end())
			{
				usersIt->second.erase(userId);
				if (usersIt->second.empty())
					m_losUsersByActor.erase(usersIt);
			}
		}
		m_losActorsByUser.erase(actorsIt);
	}

	void ServerAoiSystem::RemoveLosForActor(entt::entity actor)
	{
		auto usersIt = m_losUsersByActor.find(actor);
		if (usersIt == m_losUsersByActor.end())
			return;

		for (uint64 userId : usersIt->second)
		{
			m_losCache.erase(MakeVisibilityKey(userId, actor));
			if (auto actorsIt = m_losActorsByUser.find(userId); actorsIt != m_losActorsByUser.end())
			{
				actorsIt->second.erase(actor);
				if (actorsIt->second.empty())
					m_losActorsByUser.erase(actorsIt);
			}
		}
		m_losUsersByActor.erase(usersIt);
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
		if (actor == entt::null || !m_registry.valid(actor)
			|| m_registry.all_of<ReplicationDisabledTag>(actor)
			|| !m_dirtyActorDedup.insert(actor).second)
			return;
		m_dirtyActors.push_back(actor);
	}

	bool ServerAoiSystem::RemoveVisibleMembership(uint64 userId, entt::entity actor)
	{
		const AoiVisibilityKey key = MakeVisibilityKey(userId, actor);
		auto membershipIt = m_visibleMembership.find(key);
		if (membershipIt == m_visibleMembership.end())
			return false;

		const AoiVisibleMembershipEntry removed = membershipIt->second;
		auto actorUsersIt = m_actorVisibleUsers.find(actor);
		auto userActorsIt = m_userVisibleActors.find(userId);
		const bool indicesValid = actorUsersIt != m_actorVisibleUsers.end()
			&& userActorsIt != m_userVisibleActors.end()
			&& removed.actorUserIndex < actorUsersIt->second.size()
			&& removed.userActorIndex < userActorsIt->second.size()
			&& actorUsersIt->second[removed.actorUserIndex].userId == userId
			&& userActorsIt->second[removed.userActorIndex].actor == actor;
		JAM_ASSERT(indicesValid);
		if (!indicesValid)
		{
			m_visibleMembership.erase(membershipIt);
			return false;
		}

		auto& visibleUsers = actorUsersIt->second;
		if (removed.actorUserIndex != visibleUsers.size() - 1)
		{
			visibleUsers[removed.actorUserIndex] = visibleUsers.back();
			const AoiVisibilityKey movedKey = MakeVisibilityKey(
				visibleUsers[removed.actorUserIndex].userId,
				actor);
			auto movedIt = m_visibleMembership.find(movedKey);
			JAM_ASSERT(movedIt != m_visibleMembership.end());
			if (movedIt != m_visibleMembership.end())
				movedIt->second.actorUserIndex = removed.actorUserIndex;
		}
		visibleUsers.pop_back();
		if (visibleUsers.empty())
			m_actorVisibleUsers.erase(actorUsersIt);

		auto& visibleActors = userActorsIt->second;
		if (removed.userActorIndex != visibleActors.size() - 1)
		{
			visibleActors[removed.userActorIndex] = visibleActors.back();
			const AoiVisibilityKey movedKey = MakeVisibilityKey(
				userId,
				visibleActors[removed.userActorIndex].actor);
			auto movedIt = m_visibleMembership.find(movedKey);
			JAM_ASSERT(movedIt != m_visibleMembership.end());
			if (movedIt != m_visibleMembership.end())
				movedIt->second.userActorIndex = removed.userActorIndex;
		}
		visibleActors.pop_back();
		if (visibleActors.empty())
			m_userVisibleActors.erase(userActorsIt);

		m_visibleMembership.erase(membershipIt);
		return true;
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

		if (dead == slots.size())
		{
			m_cellActors.erase(it);
			return;
		}
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

		if (dead == slots.size())
		{
			m_cellSubscribers.erase(it);
			return;
		}
		if (dead < m_cfg.compactMinDeadCount)
			return;
		if (slots.empty() || (static_cast<float>(dead) / static_cast<float>(slots.size())) < m_cfg.compactDeadRatio)
			return;

		std::erase_if(slots, [](const AoiSubscriberSlot& slot) { return !slot.alive; });
	}

	void ServerAoiSystem::FlushPendingCompactions()
	{
		for (const uint64 cellKey : m_pendingCellActorCompactions)
			CompactCellActorsIfNeeded(cellKey);
		for (const uint64 cellKey : m_pendingCellSubscriberCompactions)
			CompactCellSubscribersIfNeeded(cellKey);

		m_pendingCellActorCompactions.clear();
		m_pendingCellSubscriberCompactions.clear();
	}
}
