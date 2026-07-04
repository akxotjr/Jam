#pragma once

namespace jam::px
{
	struct MaterialData;
	struct ShapeData;
	struct PhysicsArchetypeData;
	struct DynamicBodyData;

	class PxCreator
	{
	public:
		static PxMaterial*		CreateMaterial(const MaterialData& def);

		static PxTriangleMesh*	CreateTriangleMesh(const std::string& path);
		static PxConvexMesh*	CreateConvexMesh(const std::string& path);

		static PxShape*			CreatePrimitiveShape(const ShapeData& def, const PxMaterial& material, bool exclusive = false);
		static PxShape*			CreateTriangleMeshShape(const ShapeData& def, const PxMaterial& material, PxTriangleMesh* mesh, bool exclusive = false);
		static PxShape*			CreateConvexMeshShape(const ShapeData& def, const PxMaterial& material, PxConvexMesh* mesh, bool exclusive = false);

		static PxRigidActor*	CreateRigidActor(const PhysicsArchetypeData& def, const std::vector<PxShape*>& shapes, const DynamicBodyData* dynDef = nullptr);
	};
}
