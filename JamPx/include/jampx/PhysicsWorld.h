#pragma once

#include "PhysicsDatabase.h"
#include "jampx/ShardPxCpuDispatcher.h"
#include "jampx/PhysicsSceneSlot.h"

namespace physx
{
	class PxControllerManager;
}

namespace jam::px
{
	class PhysicsArchetypeRegistry;


	class PhysicsWorld
	{
	public:
		PhysicsWorld() = default;
		~PhysicsWorld() = default;

		void							Init(PhysicsArchetypeRegistry* registry, ShardPxCpuDispatcher* dispacter);
		void							Destroy();
		PhysicsArchetypeRegistry&		Registry() const;

		PxScene*						GetScene(ePxSceneSlot slot = ePxSceneSlot::Main) const;
		PxTaskManager*					GetTaskManager() const { return m_scenes[0] ? m_scenes[0]->getTaskManager() : nullptr; }

		void							Simulate(ePxSceneSlot slot, float elapsed) const;
		void							BeginSimulate(ePxSceneSlot slot, float elapsed, PxBaseTask* completionTask) const;
		void							EndSimulate(ePxSceneSlot slot) const;

		PxRigidActor*					CreateRigidActor(ePxSceneSlot slot, PhysicsArchetypeKey key, const PxTransform& pose, void* userData);
		void							RemoveRigidActor(ePxSceneSlot slot, PxRigidActor* actor) const;

		PxCapsuleController*			CreateController(ePxSceneSlot slot, const CCTBodyData& data, const PxVec3& pos, void* userData = nullptr);
		PxRigidDynamic*					CreateHitbox(const std::vector<ShapeHandle>& shapeHandles, const PxVec3& pos, void* userData = nullptr);
		void							RemoveController(PxController* controller);

		std::vector<SimEvent>			ConsumeSimEvents();
		std::vector<ActorId>			ConsumeAdvancdActive();

	private:
		PhysicsArchetypeRegistry*							m_registry			= nullptr;
		PxScene*											m_scenes[2]				= { nullptr, nullptr };
		PxControllerManager*								m_controllerMgrs[2]		= { nullptr, nullptr };
		std::unique_ptr<SimulationEventCallback>			m_simCallbacks[2]		= { nullptr, nullptr };

		PxUserControllerHitReport*							m_characterReportCB		= nullptr;
		PxControllerBehaviorCallback*						m_characterBehaviorCB	= nullptr;
	};
}
