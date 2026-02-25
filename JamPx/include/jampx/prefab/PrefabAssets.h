#pragma once

#include <optional>
#include <jambase/Fnv1a.h>

#include "jampx/character/CharacterMovementTypes.h"
#include "jampx/PhysicsFilter.h"


namespace jam
{
    // ---- PhysX basic
    inline void HashAppend(jam::Fnv1a32& h, const physx::PxVec3& v) noexcept
    {
        HashAppend(h, v.x);
        HashAppend(h, v.y);
        HashAppend(h, v.z);
    }

    inline void HashAppend(jam::Fnv1a32& h, const physx::PxQuat& q) noexcept
    {
        HashAppend(h, q.x);
        HashAppend(h, q.y);
        HashAppend(h, q.z);
        HashAppend(h, q.w);
    }

    inline void HashAppend(jam::Fnv1a32& h, const physx::PxTransform& t) noexcept
    {
        HashAppend(h, t.p);
        HashAppend(h, t.q);
    }
}


namespace jam::px::prefab
{
    using namespace std;

    using MaterialHandle = jam::Fnv1aHandle<struct PrefabMaterialDef, uint32>;
	using MeshHandle     = jam::Fnv1aHandle<struct PrefabMeshDef, uint32>;
	using ShapeHandle    = jam::Fnv1aHandle<struct PrefabShapeDef, uint32>;
	using TemplateHandle = jam::Fnv1aHandle<struct PrefabTemplateDef, uint32>;

    enum class ePrefabBodyKind : uint8_t
    {
        NONE,
        STATIC,
        DYNAMIC,
        KINEMATIC,  // dynamic + eKINEMATIC
        CHARACTER
    };

    enum class ePrefabSpawnPolicy : uint8_t
    {
        LEVEL_ONLY,
        RUNTIME_ONLY,
        BOTH
    };

    enum class eShapeType : uint8_t
    {
        BOX,
        SPHERE,
        CAPSULE,
        PLANE,

        CONVEX_MESH,
        TRIANGLE_MESH,

        HEIGHT_FIELD
    };

    inline bool IsPrimtiveShape(eShapeType type)
    {
        return type == eShapeType::BOX      ||
               type == eShapeType::SPHERE   ||
               type == eShapeType::CAPSULE  ||
               type == eShapeType::PLANE;
    }

    // require all field 
    struct PrefabMaterialDef
    {
        float staticFriction    = 0.5f;
        float dynamicFriction   = 0.5f;
        float restitution       = 0.1f;

        bool operator==(const PrefabMaterialDef&) const = default;
    };



    struct PrefabMeshDef
    {
        eShapeType  type = eShapeType::TRIANGLE_MESH; // TRIANGLE_MESH / CONVEX_MESH 
        string      cookedPath;

        // optional (editor/cooker metadata)
        string      srcGltfPath;
        int32       srcGltfMeshIndex = 0;
        int32       srcGltfPrimitiveIndex = 0;

        bool operator==(const PrefabMeshDef&) const = default;
    };


    struct PrefabShapeDef
    {
        // required
        eShapeType              type = eShapeType::BOX;
        PxTransform             localPose{ PxIdentity };
        MaterialHandle          material{};
        eShapeFlag              shapeFlag = eShapeFlag::SIMULATION;
        SimFD                   simFD{};
        QueryFD                 qryFD{};
        float                   contactOffset = 0.0f;
        float                   restOffset = 0.02f;
        // ~required


        PxVec3                  boxHalfExtents{ 0.5f, 0.5f, 0.5f };
        float                   sphereRadius = 0.5f;
        float                   capsuleRadius = 0.5f;
        float                   capsuleHalfHeight = 0.5f;

        MeshHandle              mesh{};
    };




    // require all field
    struct PrefabDynamicBodyDef
    {
        float                   density = 1.0f;
        bool                    useGravity = true;
        float                   linearDamping = 0.0f;
        float                   angularDamping = 0.0f;
        PxVec3                  linearVelocity{ PxZero };
        PxVec3                  angularVelocity{ PxZero };
    };



    struct PrefabCCTDef
    {
        float                   radius = 0.35f;
        float                   height = 0.75f;
        MaterialHandle          material{};

        bool                    allowCrouch = true;
        bool                    allowSlide = true;

        float                   contactOffset = 0.05f;
        float                   stepOffset = 0.01f;
        float                   slopeLimit = cosf(PxPiDivFour);

        bool                    hasHitbox = false;

        MovementConfig          movement{};
    };


    struct PrefabTemplateDef
    {
        string                  name;
        ePrefabBodyKind         kind = ePrefabBodyKind::STATIC;
        ePrefabSpawnPolicy      spawnPolicy = ePrefabSpawnPolicy::BOTH;
        bool                    allowReplication = true;
        vector<ShapeHandle>     shapes;     // at least 1

        PrefabDynamicBodyDef    dynamic{};  // only use kind == ePrefabBodyKind::DYNAMIC 

        PrefabCCTDef            cct{};      // only use kind == ePrefabBodyKind::CHARACTER. if it has cct then shapes mean hitboxes of character
    };



    static int32 QuantF(float v, float scale = 10000.f)
    {
        return static_cast<int32_t>(std::lrintf(v * scale));
    }



    // ---- Handles (예: .v가 underlying id라면)
    inline void HashAppend(jam::Fnv1a32& h, MaterialHandle v) noexcept { HashAppend(h, v.v); }
    inline void HashAppend(jam::Fnv1a32& h, MeshHandle     v) noexcept { HashAppend(h, v.v); }
    inline void HashAppend(jam::Fnv1a32& h, ShapeHandle    v) noexcept { HashAppend(h, v.v); }
    inline void HashAppend(jam::Fnv1a32& h, TemplateHandle v) noexcept { HashAppend(h, v.v); }

    // ---- vector<T>
    template<class T>
    inline void HashAppend(jam::Fnv1a32& h, const std::vector<T>& vec) noexcept
    {
        // 길이 prefix (경계 모호성 제거)
        HashAppend(h, static_cast<uint32>(vec.size()));
        for (auto& e : vec) HashAppend(h, e);
    }

    JAM_FNV1A32_HASHABLE(PrefabMaterialDef,
        &PrefabMaterialDef::staticFriction,
        &PrefabMaterialDef::dynamicFriction,
        &PrefabMaterialDef::restitution);

    JAM_FNV1A32_HASHABLE(PrefabMeshDef,
        &PrefabMeshDef::type,
        &PrefabMeshDef::cookedPath,
        &PrefabMeshDef::srcGltfPath,
        &PrefabMeshDef::srcGltfMeshIndex,
        &PrefabMeshDef::srcGltfPrimitiveIndex);

    JAM_FNV1A32_HASHABLE(PrefabShapeDef,
        &PrefabShapeDef::type,
        &PrefabShapeDef::localPose,
        &PrefabShapeDef::material,
        &PrefabShapeDef::shapeFlag,
        &PrefabShapeDef::simFD,
        &PrefabShapeDef::qryFD,
        &PrefabShapeDef::boxHalfExtents,
        &PrefabShapeDef::sphereRadius,
        &PrefabShapeDef::capsuleRadius,
        &PrefabShapeDef::capsuleHalfHeight);

    JAM_FNV1A32_HASHABLE(PrefabDynamicBodyDef,
        &PrefabDynamicBodyDef::density,
        &PrefabDynamicBodyDef::useGravity,
        &PrefabDynamicBodyDef::linearDamping,
        &PrefabDynamicBodyDef::angularDamping,
        &PrefabDynamicBodyDef::linearVelocity,
        &PrefabDynamicBodyDef::angularVelocity);

    JAM_FNV1A32_HASHABLE(PrefabCCTDef,
        &PrefabCCTDef::radius,
        &PrefabCCTDef::height,
        &PrefabCCTDef::material,
        &PrefabCCTDef::allowCrouch,
        &PrefabCCTDef::allowSlide,
        &PrefabCCTDef::contactOffset,
        &PrefabCCTDef::stepOffset,
        &PrefabCCTDef::slopeLimit,
        &PrefabCCTDef::hasHitbox/*,
        &PrefabCCTDef::movement*/);

    JAM_FNV1A32_HASHABLE(PrefabTemplateDef,
        &PrefabTemplateDef::name,
        &PrefabTemplateDef::kind,
        &PrefabTemplateDef::spawnPolicy,
        &PrefabTemplateDef::allowReplication,
        &PrefabTemplateDef::shapes,
        &PrefabTemplateDef::dynamic,
        &PrefabTemplateDef::cct)


 

    struct PhysicsPrefabAsset
    {
        int32 version = 1;

        unordered_map<MaterialHandle, PrefabMaterialDef>        materials;
        unordered_map<MeshHandle, PrefabMeshDef>                meshes;
        unordered_map<ShapeHandle, PrefabShapeDef>              shapes;
        unordered_map<TemplateHandle, PrefabTemplateDef>        templates;
    };


    struct PrefabLevelOverrides
    {
        optional<PxVec3>        linearVelocity = nullopt;
        optional<PxVec3>        angularVelocity = nullopt;
        optional<float>         linearDamping = 0.0f;
        optional<float>         angularDamping = 0.0f;
    };

    struct PrefabLevelInstanceDef
    {
        std::string             templateName;
        PxTransform             pose{ PxIdentity };
        PrefabLevelOverrides    overrides{};
    };

    struct PrefabLevelAsset
    {
        int32                           version = 1;
        std::vector<PrefabLevelInstanceDef> instances;
    };

}
