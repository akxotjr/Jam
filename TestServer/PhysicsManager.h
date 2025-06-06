#pragma once

class PhysicsManager
{
public:
	PhysicsManager();
	~PhysicsManager();

	void Init();


	physx::PxScene*				CreateScene();
	physx::PxMaterial*			CreateMaterial();
	physx::PxShape*				CreateShape();
	physx::PxRigidStatic*		CreateRigidStatic();
	physx::PxActor*				CreateRigidDynamic();



private:
	physx::PxFoundation*						m_pxFoundation = nullptr;
	physx::PxDefaultAllocator					m_allocatorCallback;
	physx::PxDefaultErrorCallback				m_errorCallback;

	physx::PxPhysics*							m_pxPhysics = nullptr;

	physx::PxSceneDesc							m_defaultSceneDesc;
};

