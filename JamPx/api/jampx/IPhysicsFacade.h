#pragma once
#include <jambase/JamTypes.h>


#include "jampx/PhysicsTypes.h"
#include "jampx/IPhysicsJobBridge.h"

#include <vector>
#include <string>


namespace jam::px
{
	class IPhysicsFacade
	{
	public:
		virtual ~IPhysicsFacade() = default;

		virtual void					Init() = 0;
		virtual void					Shutdown() = 0;
		virtual void					SetJobBridge(IPhysicsJobBridge* bridge) = 0;
		virtual void					SetPhysicsAssetPath(const std::string& path) = 0;

		virtual bool					IsStepPending() const = 0;

		virtual void					Simulate(float dt) = 0;
		virtual bool					BeginSimulate(float dt, uint64 awaitKey) = 0;
		virtual void					EndSimulate() = 0;

		virtual void					Resimulate(float dt) = 0;
		virtual bool					BeginResimulate(float dt, uint64 awaitKey) = 0;
		virtual void					EndResimulate() = 0;


		virtual bool					Spawn(ObjectId id, const SpawnDesc& desc) = 0;
		virtual bool					Despawn(ObjectId id) = 0;

		virtual eBodyType				GetBodyType(ObjectId id) const = 0;
		virtual eBodyType				FindBodyType(PhysicsArchetypeKey key) const = 0;

		virtual eMotionType				GetMotionType(ObjectId id) const = 0;
		virtual eMotionType				FindMotionType(PhysicsArchetypeKey key) const = 0;

		virtual bool					IsReplayCandidate(PhysicsArchetypeKey key) const = 0;

		virtual void					PushReplayStates(const std::vector<ActorContext>& contexts) = 0;
		virtual void					PullCorrectionState(ObjectId oid, OUT CharacterState& state) = 0;

		virtual void					PushAuthorityStates(const std::vector<ActorContext>& contexts) = 0;
		virtual void					PullProxyStates(OUT std::vector<ActorContext>& contexts) = 0;

		virtual void 					ApplyCharacterInput(ObjectId id, const CharacterInput& input) = 0;
		virtual void					ApplyReplayCharacterInput(ObjectId id, const CharacterInput& input) = 0;
		virtual void					PullPredictedState(ObjectId oid, OUT CharacterState& state) = 0;

		virtual bool					GetCharacterState(ObjectId id, CharacterState& state) const = 0;
		virtual bool					SetCharacterState(ObjectId id, const CharacterState& state) = 0;

		virtual bool					GetRigidState(ObjectId id, RigidState& state) const = 0;
		virtual bool					SetRigidState(ObjectId id, const RigidState& state) = 0;


		/// @brief LOS raycast. WORLD 지오메트리에 막히면 false, 통과하면 true
		/// @note  내부적으로 sublayer=1 (LOS) 쿼리를 사용하며 ShapeQuery::NO_LOS_BLOCK 플래그를 존중함
		virtual bool					RaycastLOS(const Vec3& from, const Vec3& to) const = 0;


		virtual HitscanResult			Hitscan(const Vec3& from, const Vec3& dir, float maxRange, uint16 teamId = 0) const = 0;


		virtual std::vector<ObjectId>	PopActiveList() = 0;
		virtual std::vector<PhysicsEvent> ConsumePhysicsEvents() = 0;

	};
}
