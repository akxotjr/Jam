#pragma once

#include <jampx/PhysicsTypes.h>
#include <entt/container/dense_set.hpp>
#include <entt/entity/sparse_set.hpp>

#include "jamnet/runtime/world/simulation/common/ActorComponents.h"

namespace jam::px
{
	class PhysicsFacade;
}

	namespace jam::net
{
	class ServerWorld;
	class ServerPhysicsSystem;
	class WorldMetrics;
	struct TickCounter;

	enum class eAoiCondition : uint8
	{
		AABB_2D,	// XZ-AABB
		AABB_3D,	// XYZ-AABB
		Circle,		// XZ-Circle
		Sphere		// 3D Sphere
	};

	struct AoiCellCoord
	{
		int32 x = 0;
		int32 z = 0;

		bool operator==(const AoiCellCoord&) const noexcept = default;
	};

	struct AoiCellCoordHash
	{
		size_t operator()(const AoiCellCoord & c) const noexcept
		{
			return (static_cast<size_t>(static_cast<uint32>(c.x)) << 32)
					^ static_cast<size_t>(static_cast<uint32>(c.z));
		}
	};


	struct AoiCellRect
	{
		int32 minX = 0;
		int32 maxX = -1;
		int32 minZ = 0;
		int32 maxZ = -1;

		bool IsEmpty() const noexcept
		{
			return minX > maxX || minZ > maxZ;
		}
	};

	struct AoiActorCellState
	{
		px::Vec3                   lastPos         = px::Vec3::Zero();
		double                     travelX         = 0.0;
		double                     travelZ         = 0.0;
		AoiCellCoord               anchorCell      = {};
		bool                       initialized     = false;
	};

	struct AoiUserCellState
	{
		px::Vec3                   lastPos        = px::Vec3::Zero();
		double                     travelX        = 0.0;
		double                     travelZ        = 0.0;
		AoiCellCoord               anchorCell     = {};
		std::vector<AoiCellCoord>  interestCells;
		bool                       initialized    = false;
	};

	struct AoiActorSlot
	{
		entt::entity                actor   = entt::null;
		bool                        alive   = false;
	};

	struct AoiSubscriberSlot
	{
		uint64                      userId  = 0;
		bool                        alive   = false;
	};

	struct AoiVisibleUserSlot
	{
		uint64                      userId  = 0;
	};

	struct AoiVisibleActorSlot
	{
		entt::entity                actor   = entt::null;
	};

	struct AoiPendingVisibility
	{
		uint64                      userId  = 0;
		entt::entity                actor   = entt::null;
		uint32                      userGeneration  = 0;
	};

	struct AoiVisibilityKey
	{
		uint64       userId = 0;
		entt::entity actor  = entt::null;

		bool operator==(const AoiVisibilityKey&) const noexcept = default;
	};

	struct AoiVisibilityKeyHash
	{
		size_t operator()(const AoiVisibilityKey& k) const noexcept
		{
			const size_t h1 = std::hash<uint64>{}(k.userId);
			const size_t h2 = std::hash<uint32>{}(static_cast<uint32>(k.actor));
			return h1 ^ (h2 + 0x9e3779b97f4a7c15ull + (h1 << 6) + (h1 >> 2));
		}
	};

	template<typename Entry>
	using AoiVisibilityMap = std::unordered_map<AoiVisibilityKey, Entry, AoiVisibilityKeyHash>;

	struct AoiLosCacheEntry
	{
		uint32  lastTick		= 0;
		bool    lastVisible		= false;
	};

	struct AoiVisibleMembershipEntry
	{
		size_t actorUserIndex    = 0;
		size_t userActorIndex    = 0;
		double lastAbsDx         = 0.0;
		double lastAbsDz         = 0.0;
		double userTravelX       = 0.0;
		double userTravelZ       = 0.0;
		double actorTravelX      = 0.0;
		double actorTravelZ      = 0.0;
		bool   certificateValid  = false;
	};

	struct UserAoiState
	{
		entt::basic_sparse_set<entt::entity>	visible{ entt::type_id<void>() };	// currently visible actors
		std::vector<ActorId>		entered;		// actors entered this tick
		std::vector<ActorId>		left;			// actors left this tick
	};

	struct AoiConfig
	{
		eAoiCondition   condition             = eAoiCondition::AABB_2D;
		float           gridCellSize          = 30.0f;      // Matches the default AOI half extent.
		float           hysteresisOffset      = 10.0f;

		float           aabbX                 = 30.0f;      // AABB half x
		float           aabbY                 = 30.0f;      // AABB half y (only AABB_3D)
		float           aabbZ                 = 30.0f;      // AABB half y
		float           radius                = 30.0f;      // Circle or Sphere radius

		bool            enableLos             = false;      // LOS inspection toggle
		uint32          losRetestTicks        = 10;
		float           losEyeOffset          = 0.9f;       // viewer(from) eye    offset (+y)
		float           losHeadOffset         = 0.9f;       // target(to)   head   offset (+y)
		float           losChestOffset        = 0.6f;       // target(to)   chest  offset (+y)
		float           losPelvisOffset       = 0.3f;       // target(to)   pelvis offset (+y). (optional)

		float           compactDeadRatio      = 0.35f;
		uint32          compactMinDeadCount   = 32;
	};

	class ServerAoiSystem
	{
	public:
		explicit ServerAoiSystem(entt::registry& world, px::PhysicsFacade* physics, WorldMetrics& metrics, ServerWorld& serverWorld, ServerPhysicsSystem& serverPhysics, TickCounter& tickCounter);

		void									Init(const AoiConfig& cfg = {});
		void									Tick();

		void									OnUserEnter(uint64 userId);
		void									OnUserLeave(uint64 userId);
		void                                    OnControlledActorChanged(uint64 userId);

		void									OnActorSpawned(entt::entity actor);
		void									OnActorDestroyed(entt::entity actor);
		bool									IsUserReady(uint64 userId) const;
		bool									IsActorRegistered(entt::entity actor) const;

		bool									IsVisible(uint64 userId, ActorId actorId) const;
		const UserAoiState*						GetState(uint64 userId) const;
		const std::vector<AoiVisibleActorSlot>* GetVisibleActors(uint64 userId) const;

	private:
		void                            ClearTransientEvents();

		void                            CollectDirtyActorsFromPhysics();

		void                            UpdateDirtyUserSubscriptions();
		void                            UpdateDirtyActorMembership();
		void                            ResolvePendingVisibility();

		void                            UpdateUserAnchorAndInterestCells(uint64 userId, const px::Vec3& userPos);
		void                            UpdateActorAnchorCell(entt::entity actor, const px::Vec3& actorPos);

		void                            OnUserCellsChanged(uint64 userId, std::span<const AoiCellCoord> oldCells, std::span<const AoiCellCoord> newCells);
		void                            OnActorCellChanged(entt::entity actor, const AoiCellCoord& oldCell, const AoiCellCoord& newCell);
		void                            EnqueueVisibilityForUserCells(uint64 userId, std::span<const AoiCellCoord> cells);
		void                            EnqueueVisibilityForActorCell(entt::entity actor, const AoiCellCoord& cell);
		void                            EnqueueVisibilityForVisibleActors(uint64 userId);
		void                            EnqueueVisibilityForVisibleUsers(entt::entity actor);

		void                            EvaluateVisibility(uint64 userId, entt::entity actor);
		bool                            PassesVisibilityTests(uint64 userId, entt::entity actor, const px::Vec3& userPos, const px::Vec3& actorPos, bool wasVisible);
		bool                            PassesLos(uint64 userId, entt::entity actor, const px::Vec3& userPos, const px::Vec3& actorPos, bool wasVisible);

		px::Vec3                        ResolveActorPosition(entt::entity actor) const;
		px::Vec3                        ResolveUserPosition(uint64 userId) const;

		AoiCellCoord                    WorldToCell(const px::Vec3& pos) const;
		AoiCellRect                     BuildInterestRect(const px::Vec3& origin) const;
		std::vector<AoiCellCoord>       BuildInterestCells(const px::Vec3& origin) const;

		uint64                          MakeCellKey(const AoiCellCoord& cell) const;

		void                            AddActorToCell(const AoiCellCoord& cell, entt::entity actor);
		void                            RemoveActorFromCell(const AoiCellCoord& cell, entt::entity actor);
		void                            AddSubscriberToCell(const AoiCellCoord& cell, uint64 userId);
		void                            RemoveSubscriberFromCell(const AoiCellCoord& cell, uint64 userId);

		void                            EnqueueVisibilityEval(uint64 userId, entt::entity actor);
		void                            RemoveLosForUser(uint64 userId);
		void                            RemoveLosForActor(entt::entity actor);
		void                            MarkUserDirty(uint64 userId);
		void                            MarkActorDirty(entt::entity actor);
		bool                            RemoveVisibleMembership(uint64 userId, entt::entity actor);

		void                            CompactCellActorsIfNeeded(uint64 cellKey);
		void                            CompactCellSubscribersIfNeeded(uint64 cellKey);
		void                            FlushPendingCompactions();
		AoiVisibilityKey                MakeVisibilityKey(uint64 userId, entt::entity actor) const;

	private:
		entt::registry&														m_registry;
		px::PhysicsFacade*													m_physics          = nullptr;
		ServerWorld&														m_world;
		ServerPhysicsSystem&												m_serverPhysics;
		TickCounter&														m_tickCounter;
		WorldMetrics&														m_metrics;
		AoiConfig															m_cfg              = {};

		std::unordered_map<uint64, UserAoiState>							m_states;
		std::unordered_map<entt::entity, AoiActorCellState>					m_actorStates;
		std::unordered_map<uint64, AoiUserCellState>						m_userStates;

		std::unordered_map<uint64, std::vector<AoiActorSlot>>				m_cellActors;
		std::unordered_map<uint64, std::vector<AoiSubscriberSlot>>			m_cellSubscribers;
		std::unordered_map<entt::entity, std::vector<AoiVisibleUserSlot>>	m_actorVisibleUsers;
		std::unordered_map<uint64, std::vector<AoiVisibleActorSlot>>		m_userVisibleActors;

		std::unordered_map<entt::entity, px::Vec3>							m_entityPositions;

		std::vector<uint64>													m_dirtyUsers;
		std::vector<entt::entity>											m_dirtyActors;
		std::vector<AoiPendingVisibility>									m_pendingVisibility;

		AoiVisibilityMap<AoiLosCacheEntry>									m_losCache;
		AoiVisibilityMap<AoiVisibleMembershipEntry>							m_visibleMembership;
		std::unordered_map<uint64, std::unordered_set<entt::entity>>		m_losActorsByUser;
		std::unordered_map<entt::entity, std::unordered_set<uint64>>		m_losUsersByActor;
		std::unordered_map<uint64, uint32>									m_userGenerations;

		entt::dense_set<uint64>												m_dirtyUserDedup;
		entt::dense_set<entt::entity>										m_dirtyActorDedup;
		std::unordered_set<AoiVisibilityKey, AoiVisibilityKeyHash>			m_pendingVisibilityDedup;
		std::unordered_set<uint64>											m_pendingCellActorCompactions;
		std::unordered_set<uint64>											m_pendingCellSubscriberCompactions;
	};
}
