#pragma once

#include "jampx/prefab/PrefabAssets.h"


namespace jam::px::prefab
{
    using namespace std;


    class PhysicsPrefabRegistry final
    {
		DECLARE_SINGLETON(PhysicsPrefabRegistry)

    public:
        void                                Init(const string& prefabPath);
        void                                Shutdown();
        void                                Clear();

        void                                Load();


        bool                                HasTemplate(TemplateHandle h) const;
        TemplateHandle                      FindHandleByName(const string& name) const;
        TemplateHandle                      FindHandleByKey(PrefabKey key) const; // PrefabKey.value == fnv1a64(template.name)
        const PrefabTemplateDef*            FindTemplateDef(TemplateHandle h) const;
        eMotionType                         GetMotionType(PrefabKey key);

		PxMaterial*                         GetMaterial(MaterialHandle h) const;
		PxTriangleMesh*                     GetTriangleMesh(MeshHandle h) const;
		PxConvexMesh*                       GetConvexMesh(MeshHandle h) const;
		PxShape*                            GetShape(ShapeHandle h) const;
		void                                GetShapes(const vector<ShapeHandle>& handles, vector<PxShape*>& shapes) const;

        PxRigidActor*                       Instantiate(const PrefabLevelInstanceDef& inst);
        PxRigidActor*                       Instantiate(TemplateHandle h, const PxTransform& worldPose, void* userData = nullptr);
        PxRigidActor*                       Instantiate(const string& name, const PxTransform& worldPose, void* userData = nullptr);

    private:
        string                                          m_prefabPath;

        PhysicsPrefabAsset                              m_asset{};

        unordered_map<string, TemplateHandle>           m_nameToHandle;
        unordered_map<uint64, TemplateHandle>           m_keyToHandle;

        unordered_map<MaterialHandle, PxMaterial*>      m_materialCache;
        unordered_map<MeshHandle, PxTriangleMesh*>      m_triMeshCache;
        unordered_map<MeshHandle, PxConvexMesh*>        m_cvxMeshCache;
        unordered_map<ShapeHandle, PxShape*>            m_shapeCache;
        unordered_map<TemplateHandle, PxRigidActor*>    m_rigidCache;
    };
}


#define PHYSICS_PREFAB_REGISTRY jam::px::prefab::PhysicsPrefabRegistry::Instance()
