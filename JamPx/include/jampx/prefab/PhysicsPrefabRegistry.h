#pragma once


#include "jampx/PhysicsAsset.h"

namespace jam::px
{
    class PhysicsPrefabRegistry final
    {
		DECLARE_SINGLETON(PhysicsPrefabRegistry)

    public:
        void                                Init(const std::string& prefabPath);
        void                                Shutdown();
        void                                Clear();

        void                                Load();

        bool                                HasTemplate(TemplateHandle h) const;
        TemplateHandle                      FindHandleByName(const std::string& name) const;
        TemplateHandle                      FindHandleByKey(PrefabKey key) const; // PrefabKey.value == fnv1a64(template.name)
        const ActorTemplateDef*             FindTemplateDef(TemplateHandle h) const;
        const ActorTemplateDef*             FindTemplateDef(PrefabKey key) const;
		eBodyType                           GetBodyType(PrefabKey key);
        eMotionType                         GetMotionType(PrefabKey key);

		PxMaterial*                         GetMaterial(MaterialHandle h) const;
		PxTriangleMesh*                     GetTriangleMesh(MeshHandle h) const;
		PxConvexMesh*                       GetConvexMesh(MeshHandle h) const;
		PxShape*                            GetShape(ShapeHandle h) const;
		void                                GetShapes(const std::vector<ShapeHandle>& handles, std::vector<PxShape*>& shapes) const;

        const ShapeDef&                     GetShapeDef(ShapeHandle h) const;
		const DynamicBodyDef&               GetDynamicBodyDef(DynamicBodyHandle h) const;
        const CCTBodyDef&                   GetCCTBodyDef(CCTBodyHandle h) const;
		const CharacterMoveConfig&          GetCharacterMoveConfig(CharacterMoveConfigHandle h) const;
		const KinematicDriverConfig&        GetKinematicDriverConfig(KinematicDriverConfigHandle h) const;
		const ProjectileConfig&             GetProjectileConfig(ProjectileConfigHandle h) const;

        
    	[[deprecated]]PxRigidActor*         Instantiate(const PhysicsLevelInstanceDef& inst);


        PxRigidActor*                       Instantiate(TemplateHandle tpl, const PxTransform& pose, void* userData = nullptr);
        PxRigidActor*                       Instantiate(const std::string& name, const PxTransform& worldPose, void* userData = nullptr);

    private:
		std::string                                          m_prefabPath;

        PhysicsAsset                                         m_asset{};

		std::unordered_map<std::string, TemplateHandle>      m_nameToHandle;
		std::unordered_map<uint64, TemplateHandle>           m_keyToHandle;

        std::unordered_map<MaterialHandle, PxMaterial*>      m_materialCache;
        std::unordered_map<MeshHandle, PxTriangleMesh*>      m_triMeshCache;
        std::unordered_map<MeshHandle, PxConvexMesh*>        m_cvxMeshCache;
        std::unordered_map<ShapeHandle, PxShape*>            m_shapeCache;
        std::unordered_map<TemplateHandle, PxRigidActor*>    m_rigidCache;
    };
}


#define PHYSICS_PREFAB_REGISTRY jam::px::PhysicsPrefabRegistry::Instance()


#define JAM_PX_MATERIAL(handle)         jam::px::PhysicsPrefabRegistry::Instance().GetMaterial(handle)
#define JAM_PX_SHAPE(handle)            jam::px::PhysicsPrefabRegistry::Instance().GetShape(handle)
#define JAM_PX_SHAPES(handles, shapes)  jam::px::PhysicsPrefabRegistry::Instance().GetShapes(handles, shapes)

#define JAM_PX_DYN_DEF(handle)          jam::px::PhysicsPrefabRegistry::Instance().GetDynamicBodyDef(handle)
#define JAM_PX_CCT_DEF(handle)          jam::px::PhysicsPrefabRegistry::Instance().GetCCTBodyDef(handle)
#define JAM_PX_CHAR_MOVE_CFG(handle)    jam::px::PhysicsPrefabRegistry::Instance().GetCharacterMoveConfig(handle)
#define JAMPX_KINE_DRIVER_CFG(handle)  jam::px::PhysicsPrefabRegistry::Instance().GetKinematicDriverConfig(handle)
#define JAMPX_PROJ_CFG(handle)         jam::px::PhysicsPrefabRegistry::Instance().GetProjectileConfig(handle)
