#pragma once


#include "jampx/PhysicsDatabase.h"

namespace jam::px
{
	class PhysicsArchetypeRegistry final
	{
	public:
		PhysicsArchetypeRegistry() = default;
		~PhysicsArchetypeRegistry() = default;

		PhysicsArchetypeRegistry(const PhysicsArchetypeRegistry&) = delete;
		PhysicsArchetypeRegistry& operator=(const PhysicsArchetypeRegistry&) = delete;

		void								Init(const std::string& assetPath, std::string_view assetName = {});
		void								Shutdown();
		void								Clear();

		void								Load();

		bool								HasArchetype(PhysicsArchetypeKey key) const;
		PhysicsArchetypeKey					FindKeyByName(const std::string& name) const;
		const PhysicsArchetypeData*			FindArchetype(PhysicsArchetypeKey key) const;
		eBodyType                           GetBodyType(PhysicsArchetypeKey key) const;
		eMotionType							GetMotionType(PhysicsArchetypeKey key) const;

		PxMaterial*                         GetMaterial(MaterialHandle h) const;
		PxTriangleMesh*                     GetTriangleMesh(MeshHandle h) const;
		PxConvexMesh*                       GetConvexMesh(MeshHandle h) const;
		PxShape*                            GetShape(ShapeHandle h) const;
		void                                GetShapes(const std::vector<ShapeHandle>& handles, std::vector<PxShape*>& shapes) const;

		const ShapeData&					GetShapeDef(ShapeHandle h) const;
		const DynamicBodyData&              GetDynamicBodyDef(DynamicBodyHandle h) const;
		const CCTBodyData&					GetCCTBodyDef(CCTBodyHandle h) const;
		const CharacterMoveConfig&          GetCharacterMoveConfig(CharacterMoveConfigHandle h) const;
		const KinematicDriverConfig&        GetKinematicDriverConfig(KinematicDriverConfigHandle h) const;
		const ProjectileConfig&             GetProjectileConfig(ProjectileConfigHandle h) const;

		PxRigidActor*						Instantiate(PhysicsArchetypeKey key, const PxTransform& pose, void* userData = nullptr);
		PxRigidActor*						Instantiate(const std::string& name, const PxTransform& worldPose, void* userData = nullptr);

    private:
		std::string												m_assetPath;
		std::string												m_assetName;
		PhysicsAssetKey											m_assetKey = {};
		PhysicsDatabase											m_db;

		std::unordered_map<std::string, PhysicsArchetypeKey>	m_nameToKey;

		std::unordered_map<MaterialHandle, PxMaterial*>			m_materialCache;
		std::unordered_map<MeshHandle, PxTriangleMesh*>			m_triMeshCache;
		std::unordered_map<MeshHandle, PxConvexMesh*>			m_cvxMeshCache;
		std::unordered_map<ShapeHandle, PxShape*>				m_shapeCache;
		std::unordered_map<PhysicsArchetypeKey, PxRigidActor*>	m_rigidCache;
	};
}
