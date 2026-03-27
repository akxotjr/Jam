#pragma once
#include <jampx/PhysicsTypes.h>
#include <jampx/IPhysicsFacade.h>

#include "NetActorComponents.h"

namespace jam::net
{
	enum class eAoiCondition : uint8
	{
		AABB_2D,	// XZ-AABB
		AABB_3D,	// XYZ-AABB
		CIRCLE,		// XZ-Circle
		SPHERE		// 3D Sphere
	};

	struct AoiConfig
	{
		eAoiCondition	condition			= eAoiCondition::AABB_2D;
		uint32			updateTicks			= 3;		// AOI recalculation interval ticks
	
		float			gridCellSize		= 50.f;		// Uniform Grid Cell size
		float			hysteresisOffset	= 10.f;		// leave threshold = enter + hysteresis

		float			aabbX				= 100.f;	// AABB half-size X 
		float			aabbY				= 100.f;	// AABB half-size Y (only 3D)
		float			aabbZ				= 100.f;	// AABB half-size Z

		float			radius				= 100.f;	// CIRCLE / SPHERE radius

		bool			enableLos			= false;	// enable LOS inspection

		// LOS Offset Calculation Fomular: pos + up * (halfHeight * offset)
		
		float			losEyeOffset		= 0.9f;		// viewer(from) eye offset (+y). from = viewerPos + up * (halfHeight * eyeOffset)
		float			losHeadOffset		= 0.9f;		// target(to) head offset (+y). to = targetPos + up * (halfHeight * headOffset)
		float			losChestOffset		= 0.6f;		// target(to) chest offset (+y). to = targetPos + up * (halfHeight * chestOffset)
		float			losPelvisOffset		= 0.3f;		// (optional) target(to) pelvis offset (+y). to = targetPos + up * (halfHeight * pelvisOffset)
	};

	/// @brief Per-User AOI state
	struct UserAoiState
	{
		unordered_set<NetId>	visible;		// currently visible set of netIds
		vector<NetId>			entered;		// new netId in this tick
		vector<NetId>			left;			// leave netId in this tick
	};

	/// @brief Server side Area-of-Interest system
	class ServerAoiSystem
	{
	public:
		explicit ServerAoiSystem(entt::registry& world, px::IPhysicsFacade* physics);

		void							Init(const AoiConfig& cfg = {});
		void							Tick();

		void							OnEnter(uint64 userId);
		void							OnLeave(uint64 userId);

		bool							IsVisible(uint64 userId, NetId netId) const;
		const UserAoiState*				GetState(uint64 userId) const;
		void							SetAlwaysVisible(NetId netId, bool always);

		

	private:
		void							Rebuild();
		void							RebuildGrid();

		void							GetCandidatesFromGrid(const px::Vec3& userPos, OUT vector<entt::entity>& out) const;
		/// @brief 조건식 검사 (bias=0: enter 임계, bias=hysteresis: leave 임계)
		bool							TestCondition(const px::Vec3& origin, const px::Vec3& target, float bias) const;

		/// @brief LOS 레이캐스트 (true = 시야 통과)
		bool							TestLos(const px::Vec3& userPos, const px::Vec3& targetPos) const;

		px::Vec3						GetEntityPosition(entt::entity e) const;
		px::Vec3						GetUserPosition(uint64 userId) const;

	private:
		entt::registry&								m_world;
		AoiConfig									m_cfg{};
		px::IPhysicsFacade*							m_physics = nullptr;

		unordered_map<uint64, UserAoiState>			m_states;
		unordered_set<NetId>						m_alwaysVisible;

		unordered_map<uint64, vector<entt::entity>> m_grid;

		unordered_map<entt::entity, px::Vec3>		m_entityPositions;
	};
}
