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

		PxCapsuleController*			CreateController(prefab::TemplateHandle templateHandle, const PxVec3& pos, void* userData, MovementConfig& outMoveCfg);
		void							RemoveController(PxController* controller);

	public:
		struct CreatedCharacter
		{
			PxCapsuleController*		controller = nullptr;
			PxRigidActor*				hitboxActor = nullptr;
			MovementConfig				moveCfg{};
		};

		CreatedCharacter				CreateCharacter(prefab::TemplateHandle templateHandle, const PxVec3& pos, void* userData);


	private:
		// Simulation event callback
		struct SimCallback final : PxSimulationEventCallback
		{
			void onConstraintBreak(PxConstraintInfo*, PxU32) override {}
			void onWake(PxActor**, PxU32) override {}
			void onSleep(PxActor**, PxU32) override {}
			void onContact(const PxContactPairHeader&, const PxContactPair*, PxU32) override {}
			void onTrigger(PxTriggerPair* pairs, PxU32 count) override
			{
				for (PxU32 i = 0; i < count; ++i)
				{
					const auto& p = pairs[i];
					(void)p;
				}
			}
			void onAdvance(const PxRigidBody* const*, const PxTransform*, const PxU32) override {}
		};


	private:
		PxScene*							m_pxScene = nullptr;
		PxControllerManager*				m_controllerManager = nullptr;
		std::unique_ptr<SimCallback>		m_simCallback;


	};
}
