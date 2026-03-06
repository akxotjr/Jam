#pragma once

#include <atomic>
#include <unordered_set>

#include "PhysicsCompletionTask.h"
#include "PhysicsWorld.h"
#include "ShardPxCpuDispacter.h"
#include "jampx/prefab/PrefabLevelLoader.h"

#include "jampx/character/CharacterMovementComponent.h"
#include "jampx/api/IPhysicsFacade.h"
#include "jampx/api/PhysicsTypes.h"

#include "jampx/PhysicsUtils.h"
#include "kinematic/KinematicMoveComponent.h"
#include "projectile/ProjectileMoveComponent.h"

namespace jam::px
{
	using namespace std;

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

		bool								LoadLevel(const string& path) override;

		void								Step(float dt) override;
		bool								BeginStep(float dt, uint64 awaitKey) override;
		void								EndStep() override;

		PhysicsHandle						Spawn(ObjectId id, const SpawnDesc& desc) override;
		void								Despawn(ObjectId id) override;

		eActorType							GetActorType(ObjectId id) const override;
		eMotionType							GetMotionType(ObjectId id) const override;

		eActorType							FindActorType(PrefabKey key) const override;
		eMotionType							FindMotionType(PrefabKey key) const override;

		bool								GetCharacterState(ObjectId id, CharacterState& state) const override;
		bool								SetCharacterState(ObjectId id, const CharacterState& state) override;

		bool								GetRigidState(ObjectId id, RigidState& state) const override;
		bool								SetRigidState(ObjectId id, const RigidState& state) override;

		void								ApplyCharacterInput(ObjectId id, const CharacterInput& input) override;

		void								AttachKinematicDriver(ObjectId id, std::unique_ptr<IKinematicDriver> driver) override;
		void								DetachKinematicDriver(ObjectId id) override;


		bool								RaycastLOS(const Vec3& from, const Vec3& to) const override;

		void								MoveKinematic(ObjectId id, const Transform& target) override;
		PhysicsHandle						SpawnProjectile(ObjectId id, const ProjectileSpawnDesc& desc) override;
		void								DespawnProjectile(ObjectId id) override;

		HitscanResult						Hitscan(const Vec3& from, const Vec3& dir, float maxRange, uint16 teamId) const override;


		std::vector<SimEvent>				ConsumeSimEvents() override;
		std::vector<ObjectId>				PopActiveList() override;

	private:
		void								MoveCharacter(float dt);
		void								MarkDirty(ObjectId id);

		void								StepProjectiles(float dt);
		void								StepKinematics(float dt);

	private:

		struct RigidEntry
		{
			PhysicsHandle					physicsHandle{};
			PxRigidActor*					actor = nullptr;
			prefab::TemplateHandle			templateHandle{};
			RigidState						state{};
			bool							isKinematic = false;

			unique_ptr<KinematicMoveComponent> mover;
		};

		struct CharacterEntry
		{
			PhysicsHandle					physicsHandle{};
			PxCapsuleController*			controller = nullptr;
			prefab::TemplateHandle			templateHandle{};
			CharacterState					state{};

			bool							isKinematic = false;

			PxRigidActor*					hitbox = nullptr;

			MoveIntent						lastIntent{};
			unique_ptr<CharacterMovementComponent> mover;
		};

		/// @brief ANALYTIC / DYN_SIM 투사체 항목.
		/// HITSCAN은 영속 항목 없이 즉발 처리.
		struct ProjectileEntry
		{
			eProjectileKind					kind{};
			PhysicsHandle					physicsHandle{};
			PxRigidActor*					actor = nullptr;
			prefab::TemplateHandle			templateHandle{};

			Vec3							velocity{};
			float							gravityScale = 1.f;
			float							maxRange = 1000.f;
			float							traveledDist = 0.f;
			uint16_t						teamId = 0;

			unique_ptr<ProjectileMoveComponent> mover;
		};

	private:
		atomic<bool>							m_inited{false};

		unique_ptr<PhysicsWorld>				m_world = nullptr;
		unique_ptr<prefab::PrefabLevelLoader>	m_levelLoader = nullptr;

		PxTaskManager*							m_taskManager = nullptr;
		IPhysicsJobBridge*						m_bridge = nullptr;
		unique_ptr<ShardPxCpuDispacter>			m_dispacter; 
		PhysicsCompletionTask					m_completionTask;
		bool									m_stepPending = false;

		unordered_map<ObjectId, RigidEntry>			m_rigidEntries;
		unordered_map<ObjectId, CharacterEntry>		m_characterEntries;

		unordered_map<ObjectId, ProjectileEntry>	m_projectileEntries;

		// ANALYTIC 투사체 히트 등 수동 push 이벤트
		std::vector<SimEvent>					m_pendingSimEvents;

		// kinematic body / setGlobalPose / character move에 의한 수동 dirty tracking
// onAdvance로 검출되지 않는 위치 변경을 보완함
		unordered_set<ObjectId>					m_dirtySet;
	};
}
