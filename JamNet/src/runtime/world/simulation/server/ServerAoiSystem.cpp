#include "pch.h"
#include "jamnet/runtime/world/simulation/server/ServerAoiSystem.h"
#include "jamnet/runtime/world/simulation/server/WorldMetrics.h"

#include <jampx/PhysicsFacade.h>

#include "jamnet/runtime/world/simulation/common/WorldContext.h"
#include "jamnet/runtime/world/simulation/server/ServerWorld.h"

#include <type_traits>

namespace jam::net
{
	ServerAoiSystem::ServerAoiSystem(
		ServerWorld& world,
		entt::registry& registry,
		px::PhysicsFacade* physics,
		WorldMetrics& metrics,
		TickCounter& tickCounter)
		: m_world(world),
		m_registry(registry),
		m_physics(physics),
		m_tickCounter(tickCounter),
		m_metrics(metrics)
	{
	}

	void ServerAoiSystem::Init(const AoiConfig& cfg)
	{
		m_cfg = cfg;

		m_actorStates.clear();
		m_userStates.clear();
		m_cellStates.clear();
		m_dirtyUsers.clear();
		m_dirtyActors.clear();
		m_pendingVisibility.clear();
		m_pendingVisibilityUsers.clear();
		m_pendingVisibilityActors.clear();
		m_losCache.clear();
		m_visibleMembership.clear();
		m_losActorsByUser.clear();
		m_losUsersByActor.clear();
		m_userGenerations.clear();
		m_dirtyUserDedup.clear();
		m_pendingVisibilityDedup.clear();

	}

	void ServerAoiSystem::Tick(std::span<const entt::entity> activeEntities)
	{
		CollectDirtyActorsFromPhysics(activeEntities);

		UpdateDirtyUserSubscriptions();
		UpdateDirtyActorMembership();
		EnqueuePendingActorVisibility();
		EnqueuePendingUserVisibility();
		ResolvePendingVisibility();
	}

	void ServerAoiSystem::RegisterSubscription(UserId userId)
	{
		if (userId == kInvalidUserId)
			return;

		if (m_userStates.contains(userId))
		{
			UnregisterSubscription(userId);
		}
		++m_userGenerations[userId];

		m_userStates.try_emplace(userId);
		MarkUserDirty(userId);
	}

	void ServerAoiSystem::UnregisterSubscription(UserId userId)
	{
		if (userId == kInvalidUserId)
			return;
		++m_userGenerations[userId];

		if (auto it = m_userStates.find(userId); it != m_userStates.end())
		{
			for (const AoiCellCoord& cell : it->second.interestCells)
				RemoveSubscriberFromCell(cell, userId);
			JAM_ASSERT(it->second.subscriberIndices.empty());

			auto& visible = it->second.aoi.visible;
			while (!visible.empty())
			{
				const entt::entity actor = *visible.begin();
				JAM_ASSERT(m_visibleMembership.contains(MakeVisibilityKey(userId, actor)));
				if (!RemoveVisibleMembership(userId, actor))
					break;
				visible.erase(actor);
			}
		}
		RemoveLosForUser(userId);
		m_userStates.erase(userId);
		m_dirtyUserDedup.erase(userId);
	}

	bool ServerAoiSystem::PreparePlayer(UserId userId, entt::entity actor)
	{
		if (m_registry.all_of<ReplicationDisabledTag>(actor))
			return false;

		if (!m_userStates.contains(userId))
			RegisterSubscription(userId);

		// Publish pending actor cell positions, including level/runtime actors spawned since the last tick.
		// Visibility evaluations for existing users remain queued for the normal AOI tick.
		UpdateDirtyActorMembership();
		if (!IsActorRegistered(actor)) return false;
		UpdateUserAnchorAndInterestCells(userId, ResolveActorPosition(actor));
		EnqueuePendingActorVisibility();
		EnqueuePendingUserVisibility();

		const auto user = m_userStates.find(userId);
		if (user == m_userStates.end() || !user->second.initialized) return false;
		for (const AoiCellCoord& cell : user->second.interestCells)
		{
			const auto actors = m_cellStates.find(MakeCellKey(cell));
			if (actors == m_cellStates.end()) continue;
			for (const AoiActorSlot& slot : actors->second.actors)
				EvaluateVisibility(userId, slot.actor);
		}
		return true;
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
		if (actor == entt::null || !m_registry.valid(actor) || m_registry.all_of<ReplicationDisabledTag>(actor))
			return;

		JAM_ASSERT(!m_actorStates.contains(actor) && !m_dirtyActors.contains(actor));
		MarkActorDirty(actor);
	}

	void ServerAoiSystem::OnActorDestroyed(entt::entity actor)
	{
		auto stIt = m_actorStates.find(actor);
		if (stIt != m_actorStates.end() && stIt->second.initialized)
			RemoveActorFromCell(stIt->second.anchorCell, actor);

		std::vector<uint64> visibleUsers;
		if (stIt != m_actorStates.end())
		{
			auto& actorVisibleUsers = stIt->second.visibleUsers;
			visibleUsers.reserve(actorVisibleUsers.size());
			while (!actorVisibleUsers.empty())
			{
				const uint64 userId = actorVisibleUsers.back().userId;
				visibleUsers.push_back(userId);
				if (!RemoveVisibleMembership(userId, actor))
					break;
			}
		}

		m_dirtyActors.remove(actor);
		m_pendingVisibilityActors.remove(actor);
		RemoveLosForActor(actor);
		m_actorStates.erase(actor);

		if (!m_registry.valid(actor) || !m_registry.all_of<ActorId>(actor))
			return;

		const ActorId actorId = m_registry.get<ActorId>(actor);
		for (uint64 userId : visibleUsers)
		{
			if (auto stateIt = m_userStates.find(userId); stateIt != m_userStates.end())
			{
				if (stateIt->second.aoi.visible.remove(actor))
				{
					stateIt->second.aoi.left.push_back(actorId);
					m_metrics.RecordAoiLeftActor();
				}
			}
		}
	}

	bool ServerAoiSystem::IsVisible(uint64 userId, ActorId actorId) const
	{
		const entt::entity actor = m_world.ResolveActor(actorId);
		if (actor != entt::null)
			if (auto it = m_userStates.find(userId); it != m_userStates.end())
				return it->second.aoi.visible.contains(actor);
		return false;
	}

	const UserAoiState* ServerAoiSystem::GetState(uint64 userId) const
	{
		if (auto it = m_userStates.find(userId); it != m_userStates.end())
			return &it->second.aoi;
		return nullptr;
	}

	void ServerAoiSystem::ClearTransientEvents()
	{
		for (AoiUserState& state : m_userStates | std::views::values)
		{
			state.aoi.entered.clear();
			state.aoi.left.clear();
		}
	}

	void ServerAoiSystem::CollectDirtyActorsFromPhysics(std::span<const entt::entity> activeEntities)
	{
		for (entt::entity actor : activeEntities)
		{
			if (actor == entt::null || !m_registry.valid(actor))
				continue;

			MarkActorDirty(actor);

			if (const auto* control = m_registry.try_get<ControlTag>(actor); control && control->userId != 0)
				MarkUserDirty(control->userId);
		}
	}

	void ServerAoiSystem::UpdateDirtyUserSubscriptions()
	{
		for (uint64 userId : m_dirtyUsers)
		{
			if (!m_userStates.contains(userId))
				continue;

			px::Vec3 userPos{};
			if (!TryResolvePlayerAnchorPosition(userId, userPos))
			{
				UnregisterSubscription(userId);
				continue;
			}
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
			UpdateActorAnchorCell(actor, actorPos);
		}

		m_dirtyActors.clear();
	}

	void ServerAoiSystem::EnqueuePendingActorVisibility()
	{
		// Publish all endpoint positions/travel before any certificate precheck.
		// Keep user visibilityQueued flags until actor fan-out has finished.
		for (entt::entity actor : m_pendingVisibilityActors)
		{
			const auto state = m_actorStates.find(actor);
			if (state == m_actorStates.end())
				continue;
			EnqueueVisibilityForActorCell(actor, state->second.anchorCell);
			EnqueueVisibilityForVisibleUsers(actor);
		}
		m_pendingVisibilityActors.clear();
	}

	void ServerAoiSystem::QueueUserVisibility(uint64 userId, AoiUserState& state)
	{
		if (state.visibilityQueued)
			return;
		state.visibilityQueued = true;
		m_pendingVisibilityUsers.push_back(userId);
	}

	void ServerAoiSystem::EnqueuePendingUserVisibility()
	{
		// Scan final actor cells after membership updates. These users own all
		// their candidate/visible relations, including actors entering this tick.
		for (uint64 userId : m_pendingVisibilityUsers)
		{
			auto user = m_userStates.find(userId);
			if (user == m_userStates.end() || !user->second.visibilityQueued)
				continue;
			EnqueueVisibilityForUserCells(userId, user->second.interestCells);
			EnqueueVisibilityForVisibleActors(userId);
			user->second.visibilityQueued = false;
		}
		m_pendingVisibilityUsers.clear();
	}

	void ServerAoiSystem::ResolvePendingVisibility()
	{
		for (const AoiPendingVisibility& pending : m_pendingVisibility)
		{
			if (pending.userId == 0 || pending.actor == entt::null)
				continue;
			auto user = m_userStates.find(pending.userId);
			if (user == m_userStates.end() || !m_registry.valid(pending.actor))
				continue;
			const auto* actorId = m_registry.try_get<ActorId>(pending.actor);
			if (!actorId)
				continue;
			if (m_userGenerations[pending.userId] != pending.userGeneration)
			{
				MarkUserDirty(pending.userId);
				continue;
			}
			EvaluateVisibility(pending.userId, user->second, pending.actor, *actorId, pending.certificateChecked);
		}

		for (const AoiUserState& state : m_userStates | std::views::values)
			m_metrics.RecordAoiNeighbors(state.aoi.visible.size());

		m_pendingVisibility.clear();
		m_pendingVisibilityDedup.clear();
	}

	void ServerAoiSystem::UpdateUserAnchorAndInterestCells(uint64 userId, const px::Vec3& userPos)
	{
		auto& userState = m_userStates[userId];

		const AoiCellCoord newAnchor = WorldToCell(userPos);
		if (!userState.initialized)
		{
			JAM_ASSERT(userState.aoi.visible.empty());
			userState.initialized = true;
			userState.lastPos	  = userPos;
			userState.anchorCell  = newAnchor;

			std::vector<AoiCellCoord> oldCells;
			std::vector<AoiCellCoord> newCells = BuildInterestCells(userPos);
			userState.interestCells = newCells;
			OnUserCellsChanged(userId, oldCells, userState.interestCells);
			QueueUserVisibility(userId, userState);
			return;
		}
		if (userState.lastPos == userPos)
			return;
		userState.travelX += std::abs(static_cast<double>(userPos.x) - static_cast<double>(userState.lastPos.x));
		userState.travelZ += std::abs(static_cast<double>(userPos.z) - static_cast<double>(userState.lastPos.z));

		if (newAnchor == userState.anchorCell)
		{
			userState.lastPos = userPos;
			QueueUserVisibility(userId, userState);
			return;
		}

		std::vector<AoiCellCoord> oldCells = userState.interestCells;
		userState.lastPos		= userPos;
		userState.anchorCell	= newAnchor;
		userState.interestCells = BuildInterestCells(userPos);

		OnUserCellsChanged(userId, oldCells, userState.interestCells);
		QueueUserVisibility(userId, userState);
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

			if (!m_pendingVisibilityActors.contains(actor))
				m_pendingVisibilityActors.push(actor);
			return;
		}
		if (actorState.lastPos == actorPos)
			return;
		actorState.travelX += std::abs(static_cast<double>(actorPos.x) - static_cast<double>(actorState.lastPos.x));
		actorState.travelZ += std::abs(static_cast<double>(actorPos.z) - static_cast<double>(actorState.lastPos.z));

		if (newAnchor == actorState.anchorCell)
		{
			actorState.lastPos = actorPos;
			if (!m_pendingVisibilityActors.contains(actor))
				m_pendingVisibilityActors.push(actor);
			return;
		}

		const AoiCellCoord oldAnchor = actorState.anchorCell;
		actorState.lastPos    = actorPos;
		actorState.anchorCell = newAnchor;

		OnActorCellChanged(actor, oldAnchor, newAnchor);
		if (!m_pendingVisibilityActors.contains(actor))
			m_pendingVisibilityActors.push(actor);
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
		}
	}

	void ServerAoiSystem::OnActorCellChanged(entt::entity actor, const AoiCellCoord& oldCell, const AoiCellCoord& newCell)
	{
		if (oldCell == newCell)
			return;

		RemoveActorFromCell(oldCell, actor);
		AddActorToCell(newCell, actor);

	}

	void ServerAoiSystem::EnqueueVisibilityForUserCells(uint64 userId, std::span<const AoiCellCoord> cells)
	{
		const auto user = m_userStates.find(userId);
		if (user == m_userStates.end())
			return;
		// Existing visible actors are queued separately, including those outside the interest cells.
		const auto& visible = user->second.aoi.visible;
		for (const AoiCellCoord& cell : cells)
		{
			const uint64 cellKey = MakeCellKey(cell);
			if (auto it = m_cellStates.find(cellKey); it != m_cellStates.end())
			{
				for (const AoiActorSlot& slot : it->second.actors)
					if (!visible.contains(slot.actor))
						EnqueueVisibilityEval(userId, slot.actor);
			}
		}
	}

	void ServerAoiSystem::EnqueueVisibilityForActorCell(entt::entity actor, const AoiCellCoord& cell)
	{
		const uint64 cellKey = MakeCellKey(cell);
		if (auto it = m_cellStates.find(cellKey); it != m_cellStates.end())
		{
			for (const AoiSubscriberSlot& slot : it->second.subscribers)
			{
				const auto user = m_userStates.find(slot.userId);
				// Visible relations are handled by the reverse index; users whose
				// anchor changed scan final cells after all actor updates.
				if (user != m_userStates.end() && !user->second.visibilityQueued
					&& !user->second.aoi.visible.contains(actor))
					EnqueueVisibilityEval(slot.userId, actor);
			}
		}
	}

	void ServerAoiSystem::EnqueueVisibilityForVisibleActors(uint64 userId)
	{
		if (auto it = m_userStates.find(userId); it != m_userStates.end())
		{
			for (const entt::entity actor : it->second.aoi.visible)
			{
				const auto actorState = m_actorStates.find(actor);
				EnqueueVisibleRelation(userId, it->second, actor,
					actorState != m_actorStates.end() ? &actorState->second : nullptr);
			}
		}
	}

	void ServerAoiSystem::EnqueueVisibilityForVisibleUsers(entt::entity actor)
	{
		if (auto it = m_actorStates.find(actor); it != m_actorStates.end())
		{
			for (const AoiVisibleUserSlot& slot : it->second.visibleUsers)
			{
				const auto user = m_userStates.find(slot.userId);
				if (user != m_userStates.end() && !user->second.visibilityQueued)
					EnqueueVisibleRelation(slot.userId, user->second, actor, &it->second);
			}
		}
	}

	bool ServerAoiSystem::PassesVisibilityCertificate(const AoiVisibleMembershipEntry& certificate, const AoiUserState& user, const AoiActorState& actor) const
	{
		if (!certificate.certificateValid || !user.initialized || !actor.initialized
			|| m_cfg.condition != eAoiCondition::AABB_2D || m_cfg.enableLos)
			return false;

		const double movementBoundX = std::max(0.0, user.travelX - certificate.userTravelX)
			+ std::max(0.0, actor.travelX - certificate.actorTravelX);
		const double movementBoundZ = std::max(0.0, user.travelZ - certificate.userTravelZ)
			+ std::max(0.0, actor.travelZ - certificate.actorTravelZ);
		constexpr double kCertificateSafetyMargin = 1.0e-4;

		return certificate.lastAbsDx + movementBoundX + kCertificateSafetyMargin <= static_cast<double>(m_cfg.aabbX + m_cfg.hysteresisOffset)
			&& certificate.lastAbsDz + movementBoundZ + kCertificateSafetyMargin <= static_cast<double>(m_cfg.aabbZ + m_cfg.hysteresisOffset);
	}

	void ServerAoiSystem::EnqueueVisibleRelation(uint64 userId, const AoiUserState&, entt::entity actor, const AoiActorState*)
	{
		// The before variant checks the certificate in EvaluateVisibility.
		EnqueueVisibilityEval(userId, actor);
	}

	void ServerAoiSystem::EvaluateVisibility(uint64 userId, entt::entity actor)
	{
		if (userId == 0 || actor == entt::null || !m_registry.valid(actor))
			return;

		auto stateIt = m_userStates.find(userId);
		if (stateIt == m_userStates.end())
			return;

		const auto* actorId = m_registry.try_get<ActorId>(actor);
		if (!actorId)
			return;
		EvaluateVisibility(userId, stateIt->second, actor, *actorId);
	}

	void ServerAoiSystem::EvaluateVisibility(uint64 userId, AoiUserState& userState, entt::entity actor, ActorId actorId, bool certificateChecked)
	{
		UserAoiState& state = userState.aoi;
		const AoiVisibilityKey visibilityKey = MakeVisibilityKey(userId, actor);
		const bool wasVisible = state.visible.contains(actor);
		auto membershipIt = m_visibleMembership.find(visibilityKey);

		const AoiUserState* userCellState = userState.initialized ? &userState : nullptr;
		const AoiActorState* actorCellState = nullptr;
		if (!certificateChecked && wasVisible
			&& membershipIt != m_visibleMembership.end()
			&& membershipIt->second.certificateValid
			&& m_cfg.condition == eAoiCondition::AABB_2D
			&& !m_cfg.enableLos)
		{
			if (const auto actorCellIt = m_actorStates.find(actor); actorCellIt != m_actorStates.end())
				actorCellState = &actorCellIt->second;

			if (userCellState && actorCellState && PassesVisibilityCertificate(membershipIt->second, *userCellState, *actorCellState))
			{
				m_metrics.RecordAoiCertificateEarlyReturn();
				return;
			}
		}

		px::Vec3 userPos{};
		if (userCellState)
			userPos = userCellState->lastPos;
		else if (!TryResolvePlayerAnchorPosition(userId, userPos))
			return;
		if (!actorCellState)
			if (const auto it = m_actorStates.find(actor); it != m_actorStates.end())
				actorCellState = &it->second;
		const px::Vec3 actorPos = actorCellState && actorCellState->initialized
			? actorCellState->lastPos
			: ResolveActorPosition(actor);
		const bool nowVisible = PassesVisibilityTests(userId, actor, userPos, actorPos, wasVisible);

		const auto refreshCertificate = [&](AoiVisibleMembershipEntry& certificate)
		{
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
				auto& visibleUsers = m_actorStates[actor].visibleUsers;
				const size_t actorUserIndex = visibleUsers.size();
				visibleUsers.push_back(AoiVisibleUserSlot{ userId });
				const auto [insertedIt, inserted] = m_visibleMembership.emplace(
					visibilityKey,
					AoiVisibleMembershipEntry{
						.actorUserIndex = actorUserIndex,
					});
				(void)inserted;
				membershipIt = insertedIt;
			}
			refreshCertificate(membershipIt->second);
			if (!wasVisible)
			{
				state.visible.push(actor);
				if (!state.entered.contains(actor))
					state.entered.push(actor);
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
			if (const auto it = m_losCache.find(key); it != m_losCache.end())
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

	bool ServerAoiSystem::TryResolvePlayerAnchorPosition(uint64 userId, px::Vec3& position) const
	{
		const entt::entity actor = m_world.GetPlayerEntity(userId);
		if (actor == entt::null || !m_registry.valid(actor))
			return false;
		if (const auto* state = m_registry.try_get<CharAuthorityState>(actor))
		{
			position = state->state.pos;
			return true;
		}
		return false;
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
		auto& slots = m_cellStates[MakeCellKey(cell)].actors;
		m_actorStates.at(actor).cellActorIndex = slots.size();
		slots.push_back(AoiActorSlot{ actor });
	}

	void ServerAoiSystem::RemoveActorFromCell(const AoiCellCoord& cell, entt::entity actor)
	{
		auto it = m_cellStates.find(MakeCellKey(cell));
		auto actorIt = m_actorStates.find(actor);
		const bool cellValid = it != m_cellStates.end() && actorIt != m_actorStates.end();
		JAM_ASSERT(cellValid);
		if (!cellValid) return;

		auto& slots = it->second.actors;
		const size_t index = actorIt->second.cellActorIndex;
		const bool indexValid = index < slots.size() && slots[index].actor == actor;
		JAM_ASSERT(indexValid);
		if (!indexValid) return;

		if (index != slots.size() - 1)
		{
			slots[index] = slots.back();
			m_actorStates.at(slots[index].actor).cellActorIndex = index;
		}
		slots.pop_back();
		if (slots.empty() && it->second.subscribers.empty())
			m_cellStates.erase(it);
	}

	void ServerAoiSystem::AddSubscriberToCell(const AoiCellCoord& cell, uint64 userId)
	{
		const uint64 cellKey = MakeCellKey(cell);
		auto& indices = m_userStates.at(userId).subscriberIndices;
		JAM_ASSERT(!indices.contains(cellKey));
		auto& slots = m_cellStates[cellKey].subscribers;
		const bool inserted = indices.emplace(cellKey, slots.size()).second;
		JAM_ASSERT(inserted);
		if (!inserted) return;
		slots.push_back(AoiSubscriberSlot{ userId });
	}

	void ServerAoiSystem::RemoveSubscriberFromCell(const AoiCellCoord& cell, uint64 userId)
	{
		const uint64 cellKey = MakeCellKey(cell);
		auto it = m_cellStates.find(cellKey);
		auto user = m_userStates.find(userId);
		const bool cellValid = it != m_cellStates.end() && user != m_userStates.end();
		JAM_ASSERT(cellValid);
		if (!cellValid) return;

		auto& indices = user->second.subscriberIndices;
		const auto entry = indices.find(cellKey);
		auto& slots = it->second.subscribers;
		const bool indexValid = entry != indices.end() && entry->second < slots.size() && slots[entry->second].userId == userId;
		JAM_ASSERT(indexValid);
		if (!indexValid) return;

		const size_t index = entry->second;
		if (index != slots.size() - 1)
		{
			slots[index] = slots.back();
			m_userStates.at(slots[index].userId).subscriberIndices.at(cellKey) = index;
		}
		slots.pop_back();
		indices.erase(entry);
		if (slots.empty() && it->second.actors.empty())
			m_cellStates.erase(it);
	}

	void ServerAoiSystem::EnqueueVisibilityEval(uint64 userId, entt::entity actor, bool certificateChecked)
	{
		m_metrics.RecordAoiEnqueueAttempt();
		const AoiVisibilityKey key = MakeVisibilityKey(userId, actor);
		if (!m_pendingVisibilityDedup.insert(key).second)
			return;

		m_metrics.RecordAoiUniqueRelation();

		m_pendingVisibility.push_back(AoiPendingVisibility{
			.userId				= userId,
			.actor				= actor,
			.userGeneration		= m_userGenerations[userId],
			.certificateChecked = certificateChecked,
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
		if (m_registry.all_of<ReplicationDisabledTag>(actor) || m_dirtyActors.contains(actor))
			return;
		m_dirtyActors.push(actor);
	}

	bool ServerAoiSystem::RemoveVisibleMembership(uint64 userId, entt::entity actor)
	{
		const AoiVisibilityKey key = MakeVisibilityKey(userId, actor);
		auto membershipIt = m_visibleMembership.find(key);
		if (membershipIt == m_visibleMembership.end())
			return false;

		const AoiVisibleMembershipEntry removed = membershipIt->second;
		auto actorIt = m_actorStates.find(actor);

		const bool indicesValid = actorIt != m_actorStates.end()
			&& removed.actorUserIndex < actorIt->second.visibleUsers.size()
			&& actorIt->second.visibleUsers[removed.actorUserIndex].userId == userId;

		JAM_ASSERT(indicesValid);
		if (!indicesValid)
		{
			m_visibleMembership.erase(membershipIt);
			return false;
		}

		auto& visibleUsers = actorIt->second.visibleUsers;
		if (removed.actorUserIndex != visibleUsers.size() - 1)
		{
			visibleUsers[removed.actorUserIndex] = visibleUsers.back();
			const AoiVisibilityKey movedKey = MakeVisibilityKey(visibleUsers[removed.actorUserIndex].userId, actor);
			auto movedIt = m_visibleMembership.find(movedKey);
			JAM_ASSERT(movedIt != m_visibleMembership.end());
			if (movedIt != m_visibleMembership.end())
				movedIt->second.actorUserIndex = removed.actorUserIndex;
		}
		visibleUsers.pop_back();

		m_visibleMembership.erase(membershipIt);
		return true;
	}

}
