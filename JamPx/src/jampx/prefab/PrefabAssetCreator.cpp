#include "pch.h"
#include "jampx/prefab/PrefabAssetCreator.h"
#include "jampx/prefab/PrefabAssets.h"

#include <fstream>


namespace jam::px::prefab
{
	namespace
	{
		static bool ReadAllBytes(const string& path, OUT vector<uint8>& out)
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
	}

	PxMaterial* PrefabAssetCreator::CreateMaterial(const PrefabMaterialDef& def)
	{
		auto* mat = PX_PHYSICS->createMaterial(def.staticFriction, def.dynamicFriction, def.restitution);
		if (!mat) throw std::runtime_error("createMaterial failed");

		return mat;
	}

	PxTriangleMesh* PrefabAssetCreator::CreateTriangleMesh(const string& path)
	{
		vector<uint8> bytes;
		if (!ReadAllBytes(path, bytes) || bytes.empty())
			return nullptr;

		PxPhysics* physics = PHYSICS_CORE.Physics();
		if (!physics)
			return nullptr;

		PxDefaultMemoryInputData input(reinterpret_cast<PxU8*>(bytes.data()), bytes.size());
		return physics->createTriangleMesh(input);
	}

	PxConvexMesh* PrefabAssetCreator::CreateConvexMesh(const string& path)
	{
		vector<uint8> bytes;
		if (!ReadAllBytes(path, bytes) || bytes.empty())
			return nullptr;

		PxPhysics* physics = PHYSICS_CORE.Physics();
		if (!physics)
			return nullptr;

		PxDefaultMemoryInputData input(reinterpret_cast<PxU8*>(bytes.data()), bytes.size());
		return physics->createConvexMesh(input);
	}

	PxShape* PrefabAssetCreator::CreatePrimitiveShape(const PrefabShapeDef& def, const PxMaterial& material)
	{
		PxPhysics* physics = PHYSICS_CORE.Physics();
		if (!physics || !def.material)
			return nullptr;

		PxShape* shape = nullptr;

		switch (def.type)
		{
		case eShapeType::BOX:
			shape = physics->createShape(PxBoxGeometry(def.boxHalfExtents), material);
			break;

		case eShapeType::SPHERE:
			shape = physics->createShape(PxSphereGeometry(def.sphereRadius), material);
			break;

		case eShapeType::CAPSULE:
			shape = physics->createShape(PxCapsuleGeometry(def.capsuleRadius, def.capsuleHalfHeight), material);
			break;

		case eShapeType::PLANE:
			shape = physics->createShape(PxPlaneGeometry(), material);
			break;

		default: return nullptr;
		}

		shape->setLocalPose(def.localPose);
		ApplyShapeFilters(*shape, def.shapeFlag, def.simFD, def.qryFD);

		return shape;
	}

	PxShape* PrefabAssetCreator::CreateTriangleMeshShape(const PrefabShapeDef& def, const PxMaterial& material, PxTriangleMesh* mesh)
	{
		PxPhysics* physics = PHYSICS_CORE.Physics();
		if (!physics)
			return nullptr;

		PxTriangleMeshGeometry geom(mesh);
		PxShape* shape = physics->createShape(geom, material);
		if (!shape) return nullptr;

		shape->setLocalPose(def.localPose);
		ApplyShapeFilters(*shape, def.shapeFlag, def.simFD, def.qryFD);

		return shape;
	}

	PxShape* PrefabAssetCreator::CreateConvexMeshShape(const PrefabShapeDef& def, const PxMaterial& material, PxConvexMesh* mesh)
	{
		PxPhysics* physics = PHYSICS_CORE.Physics();
		if (!physics)
			return nullptr;

		PxConvexMeshGeometry geom(mesh);
		PxShape* shape = physics->createShape(geom, material);
		if (!shape)
			return nullptr;

		shape->setLocalPose(def.localPose);
		ApplyShapeFilters(*shape, def.shapeFlag, def.simFD, def.qryFD);

		return shape;
	}


	PxRigidActor* PrefabAssetCreator::CreateRigidActor(const PrefabTemplateDef& def, const vector<PxShape*>& shapes)
	{
		PxPhysics* physics = PHYSICS_CORE.Physics();
		if (!physics)
			return nullptr;

		PxRigidActor* actor = nullptr;

		switch (def.kind)
		{
		case ePrefabBodyKind::STATIC:
			actor = physics->createRigidStatic(PxTransform(PxIdentity));
			break;

		case ePrefabBodyKind::DYNAMIC:
		{
			PxRigidDynamic* dyn = physics->createRigidDynamic(PxTransform(PxIdentity));
			if (!dyn) return nullptr;

			dyn->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, !def.dynamic.useGravity);
			dyn->setLinearDamping(def.dynamic.linearDamping);
			dyn->setAngularDamping(def.dynamic.angularDamping);
			dyn->setLinearVelocity(def.dynamic.linearVelocity);
			dyn->setAngularVelocity(def.dynamic.angularVelocity);

			actor = dyn;
		}
		break;

		case ePrefabBodyKind::KINEMATIC:
		{
			PxRigidDynamic* dyn = physics->createRigidDynamic(PxTransform(PxIdentity));
			if (!dyn) return nullptr;

			dyn->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);
			actor = dyn;
		}
		break;

		case ePrefabBodyKind::CHARACTER:
			// Registry에서 CHARACTER는 별도 시스템(CCT)로 처리하는 정책
			return nullptr;

		default:
			return nullptr;
		}


		if (!actor) return nullptr;

		for (PxShape* s : shapes)
		{
			if (s) actor->attachShape(*s);
		}

#ifdef _DEBUG
		actor->setActorFlag(PxActorFlag::eVISUALIZATION, true);
#endif


		return actor;
	}
}
