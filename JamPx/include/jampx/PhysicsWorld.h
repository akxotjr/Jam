#pragma once


#include "ShardPxCpuDispacter.h"
#include "jampx/api/PhysicsTypes.h"
#include "jampx/character/CharacterMovementTypes.h"



namespace physx
{
	class PxControllerManager;
}

namespace jam::px
{

	class PhysicsWorld
	{
	public:
		PhysicsWorld() = default;
		~PhysicsWorld() = default;

		void							Init(ShardPxCpuDispacter* dispacter);
		void							Destroy();

		PxScene*						GetScene() const { return m_pxScene; }
		PxTaskManager*					GetTaskManager() const { return m_pxScene->getTaskManager(); }

		void							Simulate(float elapsed) const;
		void							BeginSimulate(float elapsed, PxBaseTask* completionTask) const;
		void							EndSimulate() const;

		PxRigidActor*					CreateActor(prefab::TemplateHandle templateHandle, const PxTransform& worldPose, void* userData);
		void							RemoveActor(PxRigidActor* actor) const;

		PxCapsuleController*			CreateController(prefab::TemplateHandle templateHandle, const PxVec3& pos, void* userData, CharacterMoveConfig& outMoveCfg);
		void							RemoveController(PxController* controller);

	public:
		struct CreatedCharacter
		{
			PxCapsuleController*		controller = nullptr;
			PxRigidActor*				hitboxActor = nullptr;
			CharacterMoveConfig				moveCfg{};
		};

		CreatedCharacter				CreateCharacter(prefab::TemplateHandle templateHandle, const PxVec3& pos, void* userData);

		std::vector<SimEvent>			ConsumeSimEvents();
		std::vector<ObjectId>			ConsumeAdvancdActive();

	private:
		PxScene*									m_pxScene = nullptr;
		PxControllerManager*						m_controllerManager = nullptr;
		std::unique_ptr<SimulationEventCallback>	m_simCallback = nullptr;

		PxUserControllerHitReport*		m_characterReportCB = nullptr;
		PxControllerBehaviorCallback*	m_characterBehaviorCB = nullptr;
	};
}
