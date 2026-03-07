#pragma once
#include "PrefabAssets.h"


namespace jam::px::prefab
{
	class PrefabAssetCreator
	{
	public:
		static PxMaterial*		CreateMaterial(const PrefabMaterialDef& def);

		static PxTriangleMesh*	CreateTriangleMesh(const std::string& path);
		static PxConvexMesh*	CreateConvexMesh(const std::string& path);

		static PxShape*			CreatePrimitiveShape(const PrefabShapeDef& def, const PxMaterial& material);
		static PxShape*			CreateTriangleMeshShape(const PrefabShapeDef& def, const PxMaterial& material, PxTriangleMesh* mesh);
		static PxShape*			CreateConvexMeshShape(const PrefabShapeDef& def, const PxMaterial& materialconst, PxConvexMesh* mesh);

		static PxRigidActor*	CreateRigidActor(const PrefabTemplateDef& def, const std::vector<PxShape*>& shapes);
	};
}
