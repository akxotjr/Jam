#include "pch.h"
#include "PhysicsManager.h"

PhysicsManager::PhysicsManager()
	: m_defaultSceneDesc(physx::PxTolerancesScale())
{
	
}

void PhysicsManager::Init()
{
	// PxFoundation
	m_pxFoundation = PxCreateFoundation(PX_PHYSICS_VERSION, m_allocatorCallback, m_errorCallback);

	// PxPhysics
	m_pxPhysics = PxCreatePhysics(PX_PHYSICS_VERSION, *m_pxFoundation, physx::PxTolerancesScale(), true, nullptr);

	// PxSceneDesc
	m_defaultSceneDesc.setToDefault(physx::PxTolerancesScale());


	//m_defaultSceneDesc.gravity = physx::PxVec3(0.0f, -9.8f, 0.0f);
	//m_defaultSceneDesc.cpuDispatcher = physx::PxDefaultCpuDispatcherCreate(1);	// TODO
	//m_defaultSceneDesc.filterShader = physx::PxDefaultSimulationFilterShader;
	//m_defaultSceneDesc.flags = physx::PxSceneFlag::eENABLE_CCD | physx::PxSceneFlag::eENABLE_ACTIVE_ACTORS;
	//m_defaultSceneDesc.kineKineFilteringMode = physx::PxPairFilteringMode::eKEEP;
	//m_defaultSceneDesc.staticKineFilteringMode = physx::PxPairFilteringMode::eKEEP;

	

}
