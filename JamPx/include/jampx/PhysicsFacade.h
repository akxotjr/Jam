#pragma once

#include <atomic>

#include "PhysicsCompletionTask.h"
#include "PhysicsWorld.h"
#include "ShardPxCpuDispacter.h"
#include "jampx/prefab/PrefabLevelLoader.h"

#include "jampx/character/CharacterMovementComponent.h"
#include "jampx/api/IPhysicsFacade.h"
#include "jampx/api/PhysicsTypes.h"

#include "jampx/PhysicsUtils.h"

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

		PhysicsHandle						Spawn(ObjectKey key, const SpawnDesc& desc) override;
		void								Despawn(ObjectKey key) override;

		eBodyKind							GetKind(ObjectKey key) const override;
		eBodyKind							GetKind(PrefabKey prefab) const override;

		bool								GetCharacterState(ObjectKey key, CharacterState& state) const override;
		bool								SetCharacterState(ObjectKey key, const CharacterState& state) override;

		bool								GetRigidState(ObjectKey key, RigidState& state) const override;
		bool								SetRigidState(ObjectKey key, const RigidState& state) override;

		void								ApplyCharacterInput(ObjectKey key, const CharacterInput& input) override;

	private:
		void								MoveCharacter(float dt);

	private:

		struct RigidEntry
		{
			PhysicsHandle					physicsHandle{};
			PxRigidActor*					actor = nullptr;
			prefab::TemplateHandle			templateHandle{};
			RigidState						state{};
		};

		struct CharacterEntry
		{
			PhysicsHandle					physicsHandle{};
			PxCapsuleController*			controller = nullptr;
			prefab::TemplateHandle			templateHandle{};
			CharacterState					state{};

			bool							isKinematic = false;

			PxRigidActor*					hitboxActor = nullptr;

			MoveIntent						lastIntent{};
			unique_ptr<CharacterMovementComponent> mover;
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

		unordered_map<ObjectKey, RigidEntry, ObjectKeyHash>			m_rigidEntries;
		unordered_map<ObjectKey, CharacterEntry, ObjectKeyHash>		m_characterEntries;
	};
}
