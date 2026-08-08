#pragma once

#include "jampx/PhysicsTypes.h"

#include <memory>
#include <string>
#include <vector>

namespace jam::px
{
	enum class ePxSceneSlot : uint8;
	class IPhysicsJobBridge;

	class PhysicsFacade
	{
	public:
		PhysicsFacade();
		~PhysicsFacade();

		PhysicsFacade(const PhysicsFacade&) = delete;
		PhysicsFacade& operator=(const PhysicsFacade&) = delete;

		void								Init();
		void								Shutdown();

		void								SetJobBridge(IPhysicsJobBridge* bridge);
		void								SetPhysicsAssetPath(const std::string& path);
		bool								IsStepPending() const;

		void								Simulate(float dt);
		bool								BeginSimulate(float dt, uint64 awaitKey);
		void								EndSimulate();

		void								Resimulate(float dt);
		bool								BeginResimulate(float dt, uint64 awaitKey);
		void								EndResimulate();

		bool								Spawn(ActorId id, const SpawnDesc& desc);
		bool								Despawn(ActorId id);

		eBodyType							GetBodyType(ActorId id) const;
		eBodyType							FindBodyType(PhysicsArchetypeKey key) const;

		eMotionType							GetMotionType(ActorId id) const;
		eMotionType							FindMotionType(PhysicsArchetypeKey key) const;

		bool								IsReplayCandidate(PhysicsArchetypeKey key) const;

		void								PushReplayStates(const std::vector<ActorContext>& contexts);
		void								PullCorrectionState(ActorId oid, OUT CharacterState& state);
		
		void								PushAuthorityStates(const std::vector<ActorContext>& contexts);
		void								PullProxyStates(OUT std::vector<ActorContext>& contexts);

		void								ApplyCharacterMotorInput(ActorId id, const CharacterMotorInput& input);
		void								ApplyReplayCharacterMotorInput(ActorId id, const CharacterMotorInput& input);
		void								PullPredictedState(ActorId id, OUT CharacterState& state);


		bool								GetCharacterState(ActorId id, CharacterState& state) const;
		bool								SetCharacterState(ActorId id, const CharacterState& state);

		bool								GetRigidState(ActorId id, RigidState& state) const;
		bool								SetRigidState(ActorId id, const RigidState& state);



		bool								RaycastLOS(const Vec3& from, const Vec3& to) const;
		HitscanResult						Hitscan(const Vec3& from, const Vec3& dir, float maxRange) const;

		std::vector<PhysicsEvent>			ConsumePhysicsEvents();
		std::vector<ActorId>				PopActiveList();

	private:
		void								MarkDirty(ActorId id);
		void								FlushPendingSceneOps();
		bool								SpawnNow(ActorId id, const SpawnDesc& desc);
		bool								DespawnNow(ActorId id);

		void								StepCharacters(ePxSceneSlot slot, float dt);
		void								StepKinematics(ePxSceneSlot slot, float dt);
		void								StepProjectiles(ePxSceneSlot slot, float dt);

		void								SyncKinematics(ePxSceneSlot slot);
		void								SyncProjectiles(ePxSceneSlot slot);

	private:
		struct Impl;
		std::unique_ptr<Impl> m_impl;
	};
}
