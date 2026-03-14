#pragma once

#include <atomic>
#include <unordered_set>

#include "jampx/PhysicsCompletionTask.h"
#include "jampx/ShardPxCpuDispacter.h"

#include "jampx/PhysicsWorld.h"

#include "jampx/actor/rigid/RigidBody.h"
#include "jampx/actor/character/CharacterBody.h"

#include "jampx/prefab/PrefabLevelLoader.h"



namespace jam::px
{

	static PrefabKey MakePrefabKey(std::string_view name)
	{
		return PrefabKey{ fnv1a<uint64_t>(name) };
	}


	class PhysicsFacade : public IPhysicsFacade
	{
	public:
		PhysicsFacade() = default;
		~PhysicsFacade() override = default;

		void								Init() override;
		void								Shutdown() override;

		void								SetJobBridge(IPhysicsJobBridge* bridge) override;

		bool								LoadLevel(const std::string& path) override;

		void								Step(float dt) override;

		bool								BeginStep(float dt, uint64 awaitKey) override;
		void								EndStep() override;

		PhysicsHandle						Spawn(ObjectId id, const SpawnDesc& desc) override;
		void								Despawn(ObjectId id) override;

		eBodyType							GetBodyType(ObjectId id) const;
		eBodyType							FindBodyType(PrefabKey key) const;

		eMotionType							GetMotionType(ObjectId id) const override;
		eMotionType							FindMotionType(PrefabKey key) const override;

		bool								GetCharacterState(ObjectId id, CharacterState& state) const override;
		bool								SetCharacterState(ObjectId id, const CharacterState& state) override;

		bool								GetRigidState(ObjectId id, RigidState& state) const override;
		bool								SetRigidState(ObjectId id, const RigidState& state) override;

		void								ApplyCharacterInput(ObjectId id, const CharacterInput& input) override;


		bool								RaycastLOS(const Vec3& from, const Vec3& to) const override;


		HitscanResult						Hitscan(const Vec3& from, const Vec3& dir, float maxRange, uint16 teamId) const override;


		std::vector<SimEvent>				ConsumeSimEvents();
		std::vector<ObjectId>				PopActiveList() override;

	private:
		void								MarkDirty(ObjectId id);

		void								StepCharacters(float dt);
		void								StepKinematics(float dt);
		void								StepProjectiles(float dt);

		void								SyncKinematics();
		void								SyncProjectiles();

	private:
		std::atomic<bool>									m_inited			= false;

		std::unique_ptr<PhysicsWorld>						m_world				= nullptr;
		std::unique_ptr<PrefabLevelLoader>					m_levelLoader		= nullptr;

		PxTaskManager*										m_taskManager		= nullptr;
		IPhysicsJobBridge*									m_bridge			= nullptr;
		std::unique_ptr<ShardPxCpuDispacter>				m_dispacter; 
		PhysicsCompletionTask								m_completionTask;
		bool												m_stepPending		= false;

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
