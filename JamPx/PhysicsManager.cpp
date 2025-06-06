#include "pch.h"
#include "PhysicsManager.h"

namespace jam::px
{
	void PhysicsManager::Init()
	{
		m_pxFoundation = PxCreateFoundation(PX_PHYSICS_VERSION, m_allocatorCallback, m_errorCallback);
		m_pxPhysics = PxCreatePhysics(PX_PHYSICS_VERSION, *m_pxFoundation, PxTolerancesScale(), true, nullptr);
	}

	void PhysicsManager::Shutdown()
	{
		m_pxPhysics->release();
		m_pxFoundation->release();
	}

	PxScene* PhysicsManager::CreateDefaultScene()
	{
		PxSceneDesc sceneDesc = PxSceneDesc(PxTolerancesScale());
		sceneDesc.setToDefault(PxTolerancesScale());

		return m_pxPhysics->createScene(sceneDesc);
	}

	PxScene* PhysicsManager::CreateScene(const PxSceneDesc& sceneDesc)
	{
		return m_pxPhysics->createScene(sceneDesc);
	}

	PxMaterial* PhysicsManager::CreateDefaultMaterial()
	{
		return m_pxPhysics->createMaterial(0.5, 0.5, 1.0);
	}

	PxMaterial* PhysicsManager::CreateMaterial(PxReal staticFriction, PxReal dynamicFriction, PxReal restitution)
	{
		return m_pxPhysics->createMaterial(staticFriction, dynamicFriction, restitution);
	}

	PxRigidStatic* PhysicsManager::CreateRigidStatic(const PxVec3& position, const PxQuat& rotation)
	{
		return m_pxPhysics->createRigidStatic(PxTransform(position, rotation));
	}

	PxActor* PhysicsManager::CreateRigidDynamic(const PxVec3& position, const PxQuat& rotation)
	{
		return m_pxPhysics->createRigidDynamic(PxTransform(position, rotation));
	}
}






