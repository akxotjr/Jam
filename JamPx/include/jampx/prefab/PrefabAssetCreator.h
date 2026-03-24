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

		static PxShape*			CreatePrimitiveShape(const ShapeDef& def, const PxMaterial& material, bool exclusive = false);
		static PxShape*			CreateTriangleMeshShape(const ShapeDef& def, const PxMaterial& material, PxTriangleMesh* mesh, bool exclusive = false);
		static PxShape*			CreateConvexMeshShape(const ShapeDef& def, const PxMaterial& material, PxConvexMesh* mesh, bool exclusive = false);

		static PxRigidActor*	CreateRigidActor(const ActorTemplateDef& def, const std::vector<PxShape*>& shapes);
	};
}
