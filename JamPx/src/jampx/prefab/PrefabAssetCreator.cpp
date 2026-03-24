#include "pch.h"
#include "jampx/prefab/PrefabAssetCreator.h"
#include "jampx/PhysicsAsset.h"

#include <fstream>


namespace jam::px
{
	namespace
	{
		bool ReadAllBytes(const std::string& path, OUT std::vector<uint8>& out)
		{
			std::ifstream f(path, std::ios::binary);
			if (!f)
				return false;

			f.seekg(0, std::ios::end);
			const std::streamoff size = f.tellg();
			f.seekg(0, std::ios::beg);

			out.resize(static_cast<size_t>(size));
			f.read(reinterpret_cast<char*>(out.data()), size);
			return true;
		}

		void ApplyMotionFlags(PxRigidDynamic& dyn, const MotionFlag::Flags flags, bool isKinematic)
		{
			dyn.setActorFlag(PxActorFlag::eDISABLE_GRAVITY, flags.has_any(MotionFlag::DISABLE_GRAVITY));

			if (!isKinematic && flags.has_any(MotionFlag::ENABLE_CCD))
				dyn.setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, true);

			PxRigidDynamicLockFlags lockFlags{};
			if (flags.has_any(MotionFlag::LOCK_LINEAR_X))  lockFlags |= PxRigidDynamicLockFlag::eLOCK_LINEAR_X;
			if (flags.has_any(MotionFlag::LOCK_LINEAR_Y))  lockFlags |= PxRigidDynamicLockFlag::eLOCK_LINEAR_Y;
			if (flags.has_any(MotionFlag::LOCK_LINEAR_Z))  lockFlags |= PxRigidDynamicLockFlag::eLOCK_LINEAR_Z;
			if (flags.has_any(MotionFlag::LOCK_ANGULAR_X)) lockFlags |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_X;
			if (flags.has_any(MotionFlag::LOCK_ANGULAR_Y)) lockFlags |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_Y;
			if (flags.has_any(MotionFlag::LOCK_ANGULAR_Z)) lockFlags |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z;

			if (lockFlags)
				dyn.setRigidDynamicLockFlags(lockFlags);
		}


	}

	PxMaterial* PrefabAssetCreator::CreateMaterial(const MaterialDef& def)
	{
		auto* mat = PX_PHYSICS->createMaterial(def.staticFriction, def.dynamicFriction, def.restitution);
		if (!mat) throw std::runtime_error("createMaterial failed");

		return mat;
	}

	PxTriangleMesh* PrefabAssetCreator::CreateTriangleMesh(const std::string& path)
	{
		std::vector<uint8> bytes;
		if (!ReadAllBytes(path, bytes) || bytes.empty())
			return nullptr;

		PxPhysics* physics = PHYSICS_CORE.Physics();
		if (!physics)
			return nullptr;

		physx::PxDefaultMemoryInputData input(reinterpret_cast<PxU8*>(bytes.data()), bytes.size());
		return physics->createTriangleMesh(input);
	}

	PxConvexMesh* PrefabAssetCreator::CreateConvexMesh(const std::string& path)
	{
		std::vector<uint8> bytes;
		if (!ReadAllBytes(path, bytes) || bytes.empty())
			return nullptr;

		PxPhysics* physics = PHYSICS_CORE.Physics();
		if (!physics)
			return nullptr;

		physx::PxDefaultMemoryInputData input(reinterpret_cast<PxU8*>(bytes.data()), bytes.size());
		return physics->createConvexMesh(input);
	}

	PxShape* PrefabAssetCreator::CreatePrimitiveShape(const ShapeDef& def, const PxMaterial& material, bool exclusive)
	{
		PxPhysics* physics = PHYSICS_CORE.Physics();
		if (!physics || !def.material)
			return nullptr;

		PxShape* shape = nullptr;

		switch (def.type)
		{
		case eShapeType::Box:
			shape = physics->createShape(PxBoxGeometry(def.halfExtents), material, exclusive);
			break;

		case eShapeType::Sphere:
			shape = physics->createShape(PxSphereGeometry(def.radius), material, exclusive);
			break;

		case eShapeType::Capsule:
			shape = physics->createShape(PxCapsuleGeometry(def.radius, def.halfHeight), material, exclusive);
			break;

		case eShapeType::Plane:
			shape = physics->createShape(PxPlaneGeometry(), material, exclusive);
			break;

		default: return nullptr;
		}

		shape->setLocalPose(def.localPose);
		ApplyShapeFilters(*shape, def.shapeFlag, def.simFD, def.qryFD);

		return shape;
	}

	PxShape* PrefabAssetCreator::CreateTriangleMeshShape(const ShapeDef& def, const PxMaterial& material, PxTriangleMesh* mesh, bool exclusive)
	{
		PxPhysics* physics = PHYSICS_CORE.Physics();
		if (!physics)
			return nullptr;

		PxTriangleMeshGeometry geom(mesh);
		PxShape* shape = physics->createShape(geom, material, exclusive);
		if (!shape) return nullptr;

		shape->setLocalPose(def.localPose);
		ApplyShapeFilters(*shape, def.shapeFlag, def.simFD, def.qryFD);

		return shape;
	}

	PxShape* PrefabAssetCreator::CreateConvexMeshShape(const ShapeDef& def, const PxMaterial& material, PxConvexMesh* mesh, bool exclusive)
	{
		PxPhysics* physics = PHYSICS_CORE.Physics();
		if (!physics)
			return nullptr;

		PxConvexMeshGeometry geom(mesh);
		PxShape* shape = physics->createShape(geom, material, exclusive);
		if (!shape)
			return nullptr;

		shape->setLocalPose(def.localPose);
		ApplyShapeFilters(*shape, def.shapeFlag, def.simFD, def.qryFD);

		return shape;
	}


	PxRigidActor* PrefabAssetCreator::CreateRigidActor(const ActorTemplateDef& def, const std::vector<PxShape*>& shapes)
	{
		PxPhysics* physics = PX_PHYSICS;
		if (!physics) return nullptr;

		if (!def.IsRigid() || def.bodyType != eBodyType::Rigid)
			return nullptr;

		const auto& bodyDef = std::get<RigidBodyDef>(def.body);

		PxRigidActor* actor = nullptr;
		switch (def.motionType)
		{
		case eMotionType::Static:
			actor = physics->createRigidStatic(PxTransform(physx::PxIdentity));
			break;

		case eMotionType::Dynamic:
		case eMotionType::Kinematic:
			actor = physics->createRigidDynamic(PxTransform(physx::PxIdentity));
			break;

		default:
			return nullptr;
		}

		if (!actor) return nullptr;

		for (PxShape* s : shapes)
		{
			if (!s) continue;
			if (!actor->attachShape(*s))
			{
				actor->release();
				return nullptr;
			}
		}

		if (auto* dyn = actor->is<PxRigidDynamic>())
		{
			const bool isKinematic = (def.motionType == eMotionType::Kinematic);
			if (isKinematic)
				dyn->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);

			const DynamicBodyDef* dynDef = nullptr;
			if (bodyDef.dynamic)
				dynDef = &JAM_PX_DYN_DEF(bodyDef.dynamic);

			if (dynDef)
			{
				dyn->setLinearDamping(dynDef->linearDamping);
				dyn->setAngularDamping(dynDef->angularDamping);

				if (!isKinematic)
				{
					dyn->setLinearVelocity(dynDef->linearVelocity);
					dyn->setAngularVelocity(dynDef->angularVelocity);
				}

				// dynamic only
				if (!isKinematic && dynDef->density > 0.f)
				{
					physx::PxRigidBodyExt::updateMassAndInertia(*dyn, dynDef->density);
				}
			}

			ApplyMotionFlags(*dyn, def.motionFlags, isKinematic);
		}

#ifdef _DEBUG
		actor->setActorFlag(PxActorFlag::eVISUALIZATION, true);
#endif

		return actor;
	}
}
