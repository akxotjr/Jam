#pragma once

#include <string>
#include <nlohmann/json.hpp>
#include <nlohmann/json-schema.hpp>

namespace jam::px::prefab
{
	struct PhysicsPrefabAsset;

	using nlohmann::json;
	using nlohmann::json_schema::json_validator;


    // ----------------------------
    // Root
    // ----------------------------
    inline constexpr const char* kVersion           = "version";
    inline constexpr const char* kTemplates         = "templates";

    // ----------------------------
    // templateDef
    // ----------------------------
    inline constexpr const char* kName              = "name";
    inline constexpr const char* kKind              = "kind";
    inline constexpr const char* kSpawnPolicy       = "spawn_policy";
    inline constexpr const char* kAllowReplication  = "allow_replication";
    inline constexpr const char* kShapes            = "shapes";

    inline constexpr const char* kDynBody           = "dyn_body";
    inline constexpr const char* kCct               = "cct";

    // kind enum values
    inline constexpr const char* vKindStatic        = "static";
    inline constexpr const char* vKindDynamic       = "dynamic";
    inline constexpr const char* vKindKinematic     = "kinematic";
    inline constexpr const char* vKindCharacter     = "character";

    // spawn_policy enum values
    inline constexpr const char* vSpawnLevelOnly    = "level_only";
    inline constexpr const char* vSpawnRuntimeOnly  = "runtime_only";
    inline constexpr const char* vSpawnBoth         = "both";

    // ----------------------------
    // shapeDef
    // ----------------------------
    inline constexpr const char* kType              = "type";
    inline constexpr const char* kLocalPose         = "local_pose";
    inline constexpr const char* kMaterial          = "material";
    inline constexpr const char* kShapeFlag         = "shape_flag";
    inline constexpr const char* kSimFilter         = "sim_filter";
    inline constexpr const char* kQryFilter         = "qry_filter";
    inline constexpr const char* kContactOffset     = "contact_offset";
    inline constexpr const char* kRestOffset        = "rest_offset";

    // shape geometry params
    inline constexpr const char* kHalfExtents       = "half_extents";
    inline constexpr const char* kRadius            = "radius";
    inline constexpr const char* kHalfHeight        = "half_height";
    inline constexpr const char* kMesh              = "mesh";

    // type enum values
    inline constexpr const char* vShapeBox              = "box";
    inline constexpr const char* vShapeSphere           = "sphere";
    inline constexpr const char* vShapeCapsule          = "capsule";
    inline constexpr const char* vShapePlane            = "plane";
    inline constexpr const char* vShapeConvexMesh       = "convex_mesh";
    inline constexpr const char* vShapeTriangleMesh     = "triangle_mesh";
    inline constexpr const char* vShapeHeightField      = "height_field";

    // shape_flag enum values
    inline constexpr const char* vShapeFlagSimulation       = "simulation";
    inline constexpr const char* vShapeFlagSimulationOnly   = "simulation_only";
    inline constexpr const char* vShapeFlagTrigger          = "trigger";
    inline constexpr const char* vShapeFlagTriggerOnly      = "trigger_only";
    inline constexpr const char* vShapeFlagQueryOnly        = "query_only";

    // ----------------------------
    // materialDef
    // ----------------------------
    inline constexpr const char* kStaticFriction    = "static_friction";
    inline constexpr const char* kDynamicFriction   = "dynamic_friction";
    inline constexpr const char* kRestitution       = "restitution";

    // ----------------------------
    // simFilter / qryFilter
    // ----------------------------
    inline constexpr const char* kWord0             = "word0";
    inline constexpr const char* kWord1             = "word1";
    inline constexpr const char* kWord2             = "word2";
    inline constexpr const char* kWord3             = "word3";

    // ----------------------------
    // meshDef
    // ----------------------------
    inline constexpr const char* kSrc               = "src";
    inline constexpr const char* kMeshIndex         = "mesh_index";
    inline constexpr const char* kPrimitiveIndex    = "primitive_index";
    inline constexpr const char* kCooked            = "cooked";

    // ----------------------------
    // dynBodyDef
    // ----------------------------
    inline constexpr const char* kDensity           = "density";
    inline constexpr const char* kUseGravity        = "use_gravity";
    inline constexpr const char* kLinearDamping     = "linear_damping";
    inline constexpr const char* kAngularDamping    = "angular_damping";
    inline constexpr const char* kLinearVelocity    = "linear_velocity";
    inline constexpr const char* kAngularVelocity   = "angular_velocity";

    // ----------------------------
    // cctDef
    // ----------------------------
    inline constexpr const char* kCctRadius         = "radius";
    inline constexpr const char* kCctHeight         = "height";
    inline constexpr const char* kAllowCrouch       = "allow_crouch";
    inline constexpr const char* kAllowSlide        = "allow_slide";
    inline constexpr const char* kCctContactOffset  = "contact_offset";
    inline constexpr const char* kStepOffset        = "step_offset";
    inline constexpr const char* kSlopeLimit        = "slope_limit";
    inline constexpr const char* kHasHitbox         = "has_hitbox";
    inline constexpr const char* kMovement          = "movement";

    // ----------------------------
    // cctMovement
    // ----------------------------
    inline constexpr const char* kWalkSpeed         = "walk_speed";
    inline constexpr const char* kSprintSpeed       = "sprint_speed";
    inline constexpr const char* kCrouchSpeed       = "crouch_speed";

    inline constexpr const char* kAccelGround       = "accel_ground";
    inline constexpr const char* kAccelAir          = "accel_air";
    inline constexpr const char* kFrictionGround    = "friction_ground";
    inline constexpr const char* kGravity           = "gravity";
    inline constexpr const char* kJumpSpeed         = "jump_speed";

    inline constexpr const char* kCoyoteTimeSec     = "coyote_time_sec";
    inline constexpr const char* kJumpBufferSec     = "jump_buffer_sec";

    inline constexpr const char* kStandHeight       = "stand_height";
    inline constexpr const char* kCrouchHeight      = "crouch_height";
    inline constexpr const char* kSlideHeight       = "slide_height";
    inline constexpr const char* kCharRadius        = "radius"; // movement 내부 radius(주의: 이름 중복)

    inline constexpr const char* kSlideMinSpeed     = "slide_min_speed";
    inline constexpr const char* kSlideDurationSec  = "slide_duration_sec";
    inline constexpr const char* kSlideFriction     = "slide_friction";
    inline constexpr const char* kSlideSpeedCap     = "slide_speed_cap";

    // ----------------------------
    // transform / vec3 / quat
    // ----------------------------
    inline constexpr const char* kP = "p";
    inline constexpr const char* kQ = "q";

    inline constexpr int kVec3Size = 3;
    inline constexpr int kQuatSize = 4;

    // ----------------------------
	// JSON pointer paths (정적)
	// ----------------------------
    inline constexpr const char* pVersion   = "/version";
    inline constexpr const char* pTemplates = "/templates";

    // ----------------------------
    // JSON pointer builders (인덱스 포함)
    // ----------------------------
    inline json::json_pointer MakeTemplatePtr(size_t templateIndex)
    {
        return json::json_pointer("/templates/" + std::to_string(templateIndex));
    }

    inline json::json_pointer MakeTemplateFieldPtr(size_t templateIndex, const char* field)
    {
        return json::json_pointer("/templates/" + std::to_string(templateIndex) + "/" + field);
    }

    inline json::json_pointer MakeShapePtr(size_t templateIndex, size_t shapeIndex)
    {
        return json::json_pointer("/templates/" + std::to_string(templateIndex) + "/" + kShapes + "/" + std::to_string(shapeIndex));
    }

    inline json::json_pointer MakeShapeFieldPtr(size_t templateIndex, size_t shapeIndex, const char* field)
    {
        return json::json_pointer("/templates/" + std::to_string(templateIndex) + "/" + kShapes + "/" + std::to_string(shapeIndex) + "/" + field);
    }

    // materialDef 서브트리(주의: material은 보통 shape.material 아래에 객체로 존재)
    inline json::json_pointer MakeShapeMaterialFieldPtr(size_t templateIndex, size_t shapeIndex, const char* field)
    {
        return json::json_pointer("/templates/" + std::to_string(templateIndex) + "/" + kShapes + "/" + std::to_string(shapeIndex) + "/" + kMaterial + "/" + field);
    }

    // meshDef 서브트리(주의: mesh는 보통 shape.mesh 아래에 객체로 존재)
    inline json::json_pointer MakeShapeMeshFieldPtr(size_t templateIndex, size_t shapeIndex, const char* field)
    {
        return json::json_pointer("/templates/" + std::to_string(templateIndex) + "/" + kShapes + "/" + std::to_string(shapeIndex) + "/" + kMesh + "/" + field);
    }

    inline json::json_pointer MakeDynBodyPtr(size_t templateIndex)
    {
        return MakeTemplateFieldPtr(templateIndex, kDynBody);
    }

    inline json::json_pointer MakeDynBodyFieldPtr(size_t templateIndex, const char* field)
    {
        return json::json_pointer("/templates/" + std::to_string(templateIndex) + "/" + kDynBody + "/" + field);
    }

    inline json::json_pointer MakeCctPtr(size_t templateIndex)
    {
        return MakeTemplateFieldPtr(templateIndex, kCct);
    }

    inline json::json_pointer MakeCctFieldPtr(size_t templateIndex, const char* field)
    {
        return json::json_pointer("/templates/" + std::to_string(templateIndex) + "/" + kCct + "/" + field);
    }

    inline json::json_pointer MakeCctMovementPtr(size_t templateIndex)
    {
        return json::json_pointer("/templates/" + std::to_string(templateIndex) + "/" + kCct + "/" + kMovement);
    }

    inline json::json_pointer MakeCctMovementFieldPtr(size_t templateIndex, const char* field)
    {
        return json::json_pointer("/templates/" + std::to_string(templateIndex) + "/" + kCct + "/" + kMovement + "/" + field);
    }


	class PhysicsPrefabIO
	{
	public:
		static json					LoadPrefabJsonFromFile(const std::string& path);
		static void					SavePrefabJsonToFile(const std::string& path, const json& prefab);

		static PhysicsPrefabAsset	LoadPrefabAssetFromFile(const std::string& path);
		static PhysicsPrefabAsset	LoadPrefabAssetFromJson(const json& root);

        static json                 LoadLevelJsonFromFile(const std::string& path);
        static void                 SaveLevelJsonToFile(const std::string& path, const json& level);

        static PrefabLevelAsset     LoadLevelAssetFromFile(const std::string& path);
        static PrefabLevelAsset     LoadLevelAssetFromJson(const json& root);

#if JAMPX_WITH_EDITOR
		static const char*			PrefabSchemaPath();               // 경로 제공
		static const json&			PrefabSchemaJson();               // 스키마 1회 로드
		static void					ValidatePrefabJson(const json& prefab);

        static const char*          LevelSchemaPath();
        static const json&          LevelSchemaJson();
        static void                 ValidateLevelJson(const json& level);
#endif

	};

}
