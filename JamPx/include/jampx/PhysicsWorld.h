#pragma once


#include "jampx/ShardPxCpuDispacter.h"



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

		PxRigidActor*					CreateRigidActor(TemplateHandle tpl, const PxTransform& pose, void* userData);
		void							RemoveRigidActor(PxRigidActor* actor) const;

		PxCapsuleController*			CreateController(const CCTBodyDef& def, const PxVec3& pos, void* userData = nullptr);
		PxRigidDynamic*					CreateHitbox(const std::vector<ShapeHandle>& shapeHandles, const PxVec3& pos, void* userData = nullptr);
		void							RemoveController(PxController* controller);

		std::vector<SimEvent>			ConsumeSimEvents();
		std::vector<ObjectId>			ConsumeAdvancdActive();

	private:
		PxScene*									m_pxScene				= nullptr;
		PxControllerManager*						m_controllerManager		= nullptr;
		std::unique_ptr<SimulationEventCallback>	m_simCallback			= nullptr;

		PxUserControllerHitReport*					m_characterReportCB		= nullptr;
		PxControllerBehaviorCallback*				m_characterBehaviorCB	= nullptr;
	};
}
