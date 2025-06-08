#include "pch.h"
#include "PhysicsManager.h"

void PhysicsManager::Init()
{
	m_pxFoundation = PxCreateFoundation(PX_PHYSICS_VERSION, m_allocatorCallback, m_errorCallback);
	m_pxPhysics = PxCreatePhysics(PX_PHYSICS_VERSION, *m_pxFoundation, PxTolerancesScale(), true, nullptr);

	m_defaultMaterial = CreateMaterial(0.5, 0.5, 1.0);
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

PxMaterial* PhysicsManager::GetDefaultMaterial()
{
	return m_defaultMaterial;
}

PxMaterial* PhysicsManager::CreateMaterial(PxReal staticFriction, PxReal dynamicFriction, PxReal restitution)
{
	return m_pxPhysics->createMaterial(staticFriction, dynamicFriction, restitution);
}

PxShape* PhysicsManager::CreateShape(const ColliderInfo& info)
{
	switch (info.type)
	{
	case ColliderInfo::Type::Box:
		return m_pxPhysics->createShape(physx::PxBoxGeometry(info.box.hx, info.box.hy, info.box.hz), *m_defaultMaterial);
	case ColliderInfo::Type::Capsule:
		return m_pxPhysics->createShape(physx::PxCapsuleGeometry(info.capsule.radius, info.capsule.halfHeight), *m_defaultMaterial);
	case ColliderInfo::Type::Sphere:
		return m_pxPhysics->createShape(physx::PxSphereGeometry(info.sphere.radius), *m_defaultMaterial);
	case ColliderInfo::Type::Plane:
		return m_pxPhysics->createShape(physx::PxPlaneGeometry(), *m_defaultMaterial);
	}

	return nullptr;
}

PxRigidStatic* PhysicsManager::CreateRigidStatic(const PxVec3& position, const PxQuat& rotation)
{
	return m_pxPhysics->createRigidStatic(PxTransform(position, rotation));
}

PxActor* PhysicsManager::CreateRigidDynamic(const PxVec3& position, const PxQuat& rotation)
{
	return m_pxPhysics->createRigidDynamic(PxTransform(position, rotation));
}