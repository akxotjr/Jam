#pragma once

#include "jampx/api/PhysicsTypes.h"
#include "jampx/api/IPhysicsJobBridge.h"

namespace jam::px
{
	class IPhysicsFacade
	{
	public:
		virtual ~IPhysicsFacade() = default;

		virtual void			Init() = 0;
		virtual void			Shutdown() = 0;

		virtual void			SetJobBridge(IPhysicsJobBridge* bridge) = 0;

		virtual bool			LoadLevel(const std::string& path) = 0;

		virtual void			Step(float dt) = 0;

		virtual bool			BeginStep(float dt, uint64 awaitKey) = 0;
		virtual void			EndStep() = 0;

		virtual PhysicsHandle	Spawn(ObjectId id, const SpawnDesc& desc) = 0;
		virtual void			Despawn(ObjectId id) = 0;

		virtual eMotionType		GetMotionType(ObjectId id) const = 0;
		virtual eMotionType		FindMotionType(PrefabKey key) const = 0;

		virtual bool			GetCharacterState(ObjectId id, CharacterState& state) const = 0;
		virtual bool			SetCharacterState(ObjectId id, const CharacterState& state) = 0;

		virtual bool			GetRigidState(ObjectId id, RigidState& state) const = 0;
		virtual bool			SetRigidState(ObjectId id, const RigidState& state) = 0;

		virtual void 			ApplyCharacterInput(ObjectId id, const CharacterInput& input) = 0;

		/// @brief LOS raycast. WORLD 지오메트리에 막히면 false, 통과하면 true
		/// @note  내부적으로 sublayer=1 (LOS) 쿼리를 사용하며 ShapeQuery::NO_LOS_BLOCK 플래그를 존중함
		virtual bool			RaycastLOS(const Vec3& from, const Vec3& to) const = 0;

		virtual PhysicsHandle	SpawnProjectile(ObjectId id, const ProjectileSpawnDesc& desc) = 0;
		virtual void			DespawnProjectile(ObjectId id) = 0;

		virtual HitscanResult	Hitscan(const Vec3& from, const Vec3& dir, float maxRange, uint16 teamId = 0) const = 0;


		virtual std::vector<SimEvent> ConsumeSimEvents() = 0;
		virtual std::vector<ObjectId> PopActiveList() = 0;

	};
}
