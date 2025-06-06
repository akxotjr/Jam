#include "pch.h"
#include "PhysicsWorld.h"
#include "PhysicsManager.h"


namespace jam::px
{
	void PhysicsWorld::Init()
	{
		PxSceneDesc sceneDesc = PxSceneDesc(PxTolerancesScale());
		sceneDesc.gravity = physx::PxVec3(0.0f, -9.8f, 0.0f);
		sceneDesc.cpuDispatcher = physx::PxDefaultCpuDispatcherCreate(1);
		sceneDesc.filterShader = physx::PxDefaultSimulationFilterShader;
		sceneDesc.flags = physx::PxSceneFlag::eENABLE_CCD | physx::PxSceneFlag::eENABLE_ACTIVE_ACTORS;
		sceneDesc.kineKineFilteringMode = physx::PxPairFilteringMode::eKEEP;
		sceneDesc.staticKineFilteringMode = physx::PxPairFilteringMode::eKEEP;

		m_scene = PhysicsManager::Instance().CreateScene(sceneDesc);
	}

	void PhysicsWorld::Shutdown()
	{
		m_scene->release();
	}

	void PhysicsWorld::Simulation(float dt)
	{
		m_scene->simulate(dt);
	}

	void PhysicsWorld::AddActor(PxActor* actor)
	{
		m_scene->addActor(*actor);
	}

	void PhysicsWorld::RemoveActor(PxActor* actor)
	{
		m_scene->removeActor(*actor);
	}

	bool PhysicsWorld::Raycast()
	{
		return false;
	}

	bool PhysicsWorld::Overlap()
	{
		return false;
	}
}

