#pragma once

namespace jam::px
{
	struct MaterialDef;
	struct ShapeDef;
	struct ActorTemplateDef;

	class PrefabAssetCreator
	{
	public:
		static PxMaterial*		CreateMaterial(const MaterialDef& def);

		static PxTriangleMesh*	CreateTriangleMesh(const std::string& path);
		static PxConvexMesh*	CreateConvexMesh(const std::string& path);

		static PxShape*			CreatePrimitiveShape(const ShapeDef& def, const PxMaterial& material);
		static PxShape*			CreateTriangleMeshShape(const ShapeDef& def, const PxMaterial& material, PxTriangleMesh* mesh);
		static PxShape*			CreateConvexMeshShape(const ShapeDef& def, const PxMaterial& materialconst, PxConvexMesh* mesh);

		static PxRigidActor*	CreateRigidActor(const ActorTemplateDef& def, const std::vector<PxShape*>& shapes);
	};
}
