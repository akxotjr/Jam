#pragma once

#include <atomic>
#include <unordered_set>

#include "jampx/PhysicsCompletionTask.h"
#include "jampx/PhysicsTypes.h"
#include "jampx/ShardPxCpuDispacter.h"
#include "jampx/PhysicsWorld.h"
#include "jampx/actor/rigid/RigidBody.h"
#include "jampx/actor/character/CharacterBody.h"
#include "jampx/prefab/PhysicsArchetypeRegistry.h"


namespace jam::px
{
	class PhysicsFacade : public IPhysicsFacade
	{
	public:
		PhysicsFacade() = default;
		~PhysicsFacade() override = default;

		void								Init() override;
		void								Shutdown() override;

		void								SetJobBridge(IPhysicsJobBridge* bridge) override;
		void								SetPhysicsAssetPath(const std::string& path) override;
		bool								IsStepPending() const override;

		void								Simulate(float dt) override;
		bool								BeginSimulate(float dt, uint64 awaitKey) override;
		void								EndSimulate() override;

		void								Resimulate(float dt) override;
		bool								BeginResimulate(float dt, uint64 awaitKey) override;
		void								EndResimulate() override;

		bool								Spawn(ObjectId id, const SpawnDesc& desc) override;
		bool								Despawn(ObjectId id) override;

		eBodyType							GetBodyType(ObjectId id) const override;
		eBodyType							FindBodyType(PhysicsArchetypeKey key) const override;

		eMotionType							GetMotionType(ObjectId id) const override;
		eMotionType							FindMotionType(PhysicsArchetypeKey key) const override;

		bool								IsReplayCandidate(PhysicsArchetypeKey key) const override;

		void								PushReplayStates(const std::vector<ActorContext>& contexts) override;
		void								PullCorrectionState(ObjectId oid, OUT CharacterState& state) override;
		
		void								PushAuthorityStates(const std::vector<ActorContext>& contexts) override;
		void								PullProxyStates(OUT std::vector<ActorContext>& contexts) override;

		void								ApplyCharacterInput(ObjectId id, const CharacterInput& input) override;
		void								ApplyReplayCharacterInput(ObjectId id, const CharacterInput& input) override;
		void								PullPredictedState(ObjectId oid, OUT CharacterState& state) override;


		bool								GetCharacterState(ObjectId id, CharacterState& state) const override;
		bool								SetCharacterState(ObjectId id, const CharacterState& state) override;

		bool								GetRigidState(ObjectId id, RigidState& state) const override;
		bool								SetRigidState(ObjectId id, const RigidState& state) override;



		bool								RaycastLOS(const Vec3& from, const Vec3& to) const override;
		HitscanResult						Hitscan(const Vec3& from, const Vec3& dir, float maxRange, uint16 teamId) const override;

		std::vector<PhysicsEvent>			ConsumePhysicsEvents() override;
		std::vector<ObjectId>				PopActiveList() override;

	private:
		void								MarkDirty(ObjectId id);
		void								FlushPendingSceneOps();
		bool								SpawnNow(ObjectId id, const SpawnDesc& desc);
		bool								DespawnNow(ObjectId id);

		void								StepCharacters(ePxSceneSlot slot, float dt);
		void								StepKinematics(ePxSceneSlot slot, float dt);
		void								StepProjectiles(ePxSceneSlot slot, float dt);

		void								SyncKinematics(ePxSceneSlot slot);
		void								SyncProjectiles(ePxSceneSlot slot);

		std::optional<PxTransform>			ResolveTargetPose(ObjectId oid);

	private:
		enum class ePendingSceneOpType
		{
			Spawn,
			Despawn,
		};

		struct PendingSceneOp
		{
			ePendingSceneOpType type = ePendingSceneOpType::Spawn;
			ObjectId			id	 = INVALID_OBJ_ID;
			SpawnDesc			desc = {};
		};


	private:
		std::atomic<bool>									m_inited			= false;

		std::unique_ptr<PhysicsWorld>						m_world				= nullptr;
		PhysicsArchetypeRegistry							m_registry			= {};

		PxTaskManager*										m_taskManager		= nullptr;
		IPhysicsJobBridge*									m_bridge			= nullptr;
		std::string											m_physicsAssetPath;
		std::unique_ptr<ShardPxCpuDispacter>				m_dispacter; 
		PhysicsCompletionTask								m_completionTask;
		bool												m_stepPending		= false;
		std::vector<PendingSceneOp>							m_pendingSceneOps;

		std::unordered_map<ObjectId, RigidBody>				m_rigidMap;			// None / Static / Dynamic -> PhysX Simulate
		std::unordered_map<ObjectId, RigidBody>				m_kinematicMap;		// Kinematic -> StepKinematics()
		std::unordered_map<ObjectId, RigidBody>				m_projectileMap;	// Projectiles -> StepProjectiles()
		std::unordered_map<ObjectId, CharacterBody>			m_cctMap;			// Local/AI character -> MoveCharacters()
		std::unordered_map<ObjectId, CharacterBody>			m_remoteCctMap;		// Remote character -> SetCharacterState()'s Teleport. no tick

		// ANALYTIC 투사체 히트 등 수동 push 이벤트
		std::vector<SimEvent>								m_pendingSimEvents;

		// kinematic body / setGlobalPose / character move에 의한 수동 dirty tracking
		// onAdvance로 검출되지 않는 위치 변경을 보완함
		std::unordered_set<ObjectId>						m_dirtySet;
	};
}
