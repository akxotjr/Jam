#include "pch.h"
#include "jamnet/sync/replication/ServerAoiSystem.h"
#include "jamnet/sync/replication/NetActorComponents.h"
#include "jamnet/sync/replication/NetWorldContext.h"


namespace jam::net
{
	namespace 
	{
		constexpr uint64 MakeGridKey(int32 x, int32 z) noexcept
		{
			return  (static_cast<uint64>(static_cast<uint32>(x)) << 32) | static_cast<uint32>(z);
		}

		std::pair<int32, int32> WorldToCell(float wx, float wz, float cellSize) noexcept
		{
			return
			{
				static_cast<int32>(std::floor(wx / cellSize)),
				static_cast<int32>(std::floor(wz / cellSize))
			};
		}

		/// @brief 조건식별 XZ leave 반-크기 계산 (격자 쿼리 범위 결정에 사용)
		std::pair<float, float> CalcLeaveHalfExtents(const AoiConfig& cfg) noexcept
		{
			switch (cfg.condition)
			{
			case eAoiCondition::CIRCLE:
			case eAoiCondition::SPHERE:
			{
				const float r = cfg.radius + cfg.hysteresisOffset;
				return { r, r };
			}
			case eAoiCondition::AABB_2D:
			case eAoiCondition::AABB_3D:
				return { cfg.aabbX + cfg.hysteresisOffset, cfg.aabbZ + cfg.hysteresisOffset };
			}
			return { cfg.radius + cfg.hysteresisOffset, cfg.radius + cfg.hysteresisOffset };
		}
	}

	ServerAoiSystem::ServerAoiSystem(entt::registry& world, px::IPhysicsFacade* physics)
		: m_world(world), m_physics(physics)
	{
	}

	void ServerAoiSystem::Init(const AoiConfig& cfg)
	{
		m_cfg = cfg;

		m_states.clear();
		m_alwaysVisible.clear();
		m_grid.clear();
		m_entityPositions.clear();
	}

	void ServerAoiSystem::Tick()
	{
		const uint32 tick = m_world.ctx().get<TickCounter>().tick;

		const uint32 interval = std::max(1u, m_cfg.updateTicks);
		if ((tick % interval) == 0)
			Rebuild();
		else
		{
			for (auto& s : m_states | std::views::values)
			{
				s.entered.clear(); 
				s.left.clear();
			}
		}
	}

	void ServerAoiSystem::OnEnter(uint64 userId)
	{
		if (userId == 0) return;
		auto& s = m_states[userId];
		s.visible.clear();
		s.entered.clear();
		s.left.clear();
	}

	void ServerAoiSystem::OnLeave(uint64 userId)
	{
		m_states.erase(userId);
	}


	bool ServerAoiSystem::IsVisible(uint64 userId, NetId netId) const
	{
		if (m_alwaysVisible.contains(netId)) return true;
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

	void ServerAoiSystem::SetAlwaysVisible(NetId netId, bool always)
	{
		if (always) m_alwaysVisible.insert(netId);
		else        m_alwaysVisible.erase(netId);
	}

	// ============================================================
	// Rebuild
	// ============================================================
	void ServerAoiSystem::Rebuild()
	{
		RebuildGrid();

		std::unordered_map<uint64, px::Vec3> userPositions;
		{
			auto view = m_world.view<ControlTag>();
			userPositions.reserve(m_states.size());
			for (auto e : view)
			{
				const uint64 userId = view.get<ControlTag>(e).userId;
				if (userId == 0)
					continue;

				userPositions[userId] = GetEntityPosition(e);
			}
		}

		std::vector<entt::entity> candidates;
		candidates.reserve(64);

		for (auto& [userId, state] : m_states)
		{
			const auto posIt = userPositions.find(userId);
			const px::Vec3 userPos = (posIt != userPositions.end()) ? posIt->second : px::Vec3::Zero();

			// 1. 격자 기반 후보 수집 (leave 임계 범위)
			candidates.clear();
			GetCandidatesFromGrid(userPos, candidates);

			// 2. 새 visible 집합 구성
			std::unordered_set<NetId> newVisible;
			newVisible.reserve(state.visible.size() + 16);

			// alwaysVisible 삽입
			//{
			//	auto view = m_world.view<NetId>();
			//	for (auto e : view)
			//	{
			//		const NetId netId = m_world.get<NetId>(e);
			//		if (m_alwaysVisible.contains(netId))
			//			newVisible.insert(netId);
			//	}
			//}
			for (const NetId& netId : m_alwaysVisible)
			{
				newVisible.insert(netId);
			}

			// 3. 후보별 조건식 + hysteresis + LOS 검사
			for (auto e : candidates)
			{
				if (!m_world.valid(e) || !m_world.all_of<NetId>(e))
					continue;

				const NetId netId = m_world.get<NetId>(e);
				if (m_alwaysVisible.contains(netId))
					continue;

				auto posIt = m_entityPositions.find(e);
				if (posIt == m_entityPositions.end())
					continue;
				const px::Vec3& entityPos = posIt->second;

				// Hysteresis: 이미 보이던 엔티티는 leave 임계(더 넓음) 적용
				const bool  wasVisible = state.visible.contains(netId);
				const float bias	   = wasVisible ? m_cfg.hysteresisOffset : 0.f;

				if (!TestCondition(userPos, entityPos, bias))
					continue;

				// LOS: 범위 조건 통과 후에만 레이캐스트 (비용 최소화)
				if (m_cfg.enableLos && m_physics)
				{
					if (!TestLos(userPos, entityPos))
						continue;
				}

				newVisible.insert(netId);
			}

			// 4. entered / left diff
			state.entered.clear();
			state.left.clear();

			for (NetId id : newVisible)
				if (!state.visible.contains(id))
					state.entered.push_back(id);

			for (NetId id : state.visible)
				if (!newVisible.contains(id))
					state.left.push_back(id);

			if (!state.entered.empty() || !state.left.empty())
			{
				JAMNET_LOG_INFO(
					"[AOI][Server] user={} pos=({:.2f},{:.2f},{:.2f}) candidates={} visible={} entered={} left={}",
					userId,
					userPos.x,
					userPos.y,
					userPos.z,
					candidates.size(),
					newVisible.size(),
					state.entered.size(),
					state.left.size());
			}

			state.visible = std::move(newVisible);
		}
	}

	// ============================================================
	// Uniform Grid
	// ============================================================
	void ServerAoiSystem::RebuildGrid()
	{
		m_grid.clear();
		m_entityPositions.clear();

		const float cellSize = std::max(1.f, m_cfg.gridCellSize);

		auto view = m_world.view<NetId>();
		for (auto e : view)
		{
			const px::Vec3 pos = GetEntityPosition(e);
			m_entityPositions.emplace(e, pos);

			auto [cx, cz] = WorldToCell(pos.x, pos.z, cellSize);
			m_grid[MakeGridKey(cx, cz)].push_back(e);
		}
	}

	void ServerAoiSystem::GetCandidatesFromGrid(const px::Vec3& userPos, std::vector<entt::entity>& out) const
	{
		const float cellSize = std::max(1.f, m_cfg.gridCellSize);

		const auto [leaveHX, leaveHZ] = CalcLeaveHalfExtents(m_cfg);

		const int32 minCX = static_cast<int32>(std::floor((userPos.x - leaveHX) / cellSize));
		const int32 maxCX = static_cast<int32>(std::floor((userPos.x + leaveHX) / cellSize));
		const int32 minCZ = static_cast<int32>(std::floor((userPos.z - leaveHZ) / cellSize));
		const int32 maxCZ = static_cast<int32>(std::floor((userPos.z + leaveHZ) / cellSize));

		for (int32 cx = minCX; cx <= maxCX; ++cx)
			for (int32 cz = minCZ; cz <= maxCZ; ++cz)
				if (auto it = m_grid.find(MakeGridKey(cx, cz)); it != m_grid.end())
					out.insert(out.end(), it->second.begin(), it->second.end());
	}

	// ============================================================
	// 조건식 (Hysteresis bias 포함)
	// ============================================================
	bool ServerAoiSystem::TestCondition(const px::Vec3& origin, const px::Vec3& target, float bias) const
	{
		switch (m_cfg.condition)
		{
		case eAoiCondition::CIRCLE:
		{
			const float r  = m_cfg.radius + bias;
			const float dx = target.x - origin.x;
			const float dz = target.z - origin.z;
			return (dx * dx + dz * dz) <= (r * r);
		}
		case eAoiCondition::SPHERE:
		{
			const float r  = m_cfg.radius + bias;
			const float dx = target.x - origin.x;
			const float dy = target.y - origin.y;
			const float dz = target.z - origin.z;
			return (dx * dx + dy * dy + dz * dz) <= (r * r);
		}
		case eAoiCondition::AABB_2D:
		{
			return std::abs(target.x - origin.x) <= (m_cfg.aabbX + bias) &&
				   std::abs(target.z - origin.z) <= (m_cfg.aabbZ + bias);
		}
		case eAoiCondition::AABB_3D:
		{
			return std::abs(target.x - origin.x) <= (m_cfg.aabbX + bias) &&
				   std::abs(target.y - origin.y) <= (m_cfg.aabbY + bias) &&
				   std::abs(target.z - origin.z) <= (m_cfg.aabbZ + bias);
		}
		}
		return false;
	}

	// ============================================================
	// LOS
	// ============================================================
	bool ServerAoiSystem::TestLos(const px::Vec3& userPos, const px::Vec3& targetPos) const
	{
		if (!m_physics) return true;

		const px::Vec3 eyeOffset{ 0.f, m_cfg.losEyeOffset, 0.f };
		return m_physics->RaycastLOS(userPos + eyeOffset, targetPos + eyeOffset);
	}

	// ============================================================
	// 위치 쿼리
	// ============================================================
	px::Vec3 ServerAoiSystem::GetEntityPosition(entt::entity e) const
	{
		if (const auto* cs = m_world.try_get<px::CharacterState>(e)) return cs->pos;
		if (const auto* rs = m_world.try_get<px::RigidState>(e))	 return rs->pose.p;
		return {};
	}

	px::Vec3 ServerAoiSystem::GetUserPosition(uint64 userId) const
	{
		auto view = m_world.view<ControlTag>();
		for (auto e : view)
		{
			if (m_world.get<ControlTag>(e).userId == userId)
				return GetEntityPosition(e);
		}
		return {};
	}
}
