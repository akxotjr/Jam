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
    //  Root
    // ----------------------------

    inline constexpr const char* k_version          = "version";
    inline constexpr const char* k_templates        = "templates";

    // ----------------------------
    //  TemplateDef
    // ----------------------------

    inline constexpr const char* k_name             = "name";
    inline constexpr const char* k_actorType        = "actor_type";
    inline constexpr const char* k_representation   = "representation";
    inline constexpr const char* k_motionType       = "motion_type";
    inline constexpr const char* k_bodyFlags        = "body_flags";
    inline constexpr const char* k_spawnPolicy      = "spawn_policy";
    inline constexpr const char* k_allowReplication = "allow_replication";



    // ---- actor type enum values ----

    inline constexpr const char* k_genericActor     = "generic";
    inline constexpr const char* k_projectile       = "projectile";
    inline constexpr const char* k_character        = "character";

    // ---- motion type enum values ----

    inline constexpr const char* k_static           = "static";
    inline constexpr const char* k_dynamic          = "dynamic";
    inline constexpr const char* k_kinematic        = "kinematic";

    // ---- body flag enum values ----

    inline constexpr const char* k_disableGravity   = "disable_gravity";
    inline constexpr const char* k_enableCCD        = "enable_ccd";
    inline constexpr const char* k_lockLinearX      = "lock_linear_x";
    inline constexpr const char* k_lockLinearY      = "lock_linear_y";
    inline constexpr const char* k_lockLinearZ      = "lock_linear_z";
    inline constexpr const char* k_lockAngularX     = "lock_angular_x";
    inline constexpr const char* k_lockAngularY     = "lock_angular_y";
    inline constexpr const char* k_lockAngularZ     = "lock_angular_z";

    // ---- spawn policy enum values ----

    inline constexpr const char* k_spawnLevelOnly   = "level_only";
    inline constexpr const char* k_spawnRuntimeOnly = "runtime_only";
    inline constexpr const char* k_spawnBoth        = "both";

    // ----------------------------
    //  ShapeDef
    // ----------------------------

    inline constexpr const char* k_shapeType         = "shape_type";
    inline constexpr const char* k_localPose         = "local_pose";
    inline constexpr const char* k_material          = "material";
    inline constexpr const char* k_shapeFlag         = "shape_flag";
    inline constexpr const char* k_simFilter         = "sim_filter";
    inline constexpr const char* k_qryFilter         = "qry_filter";
    inline constexpr const char* k_contactOffset     = "contact_offset";
    inline constexpr const char* k_restOffset        = "rest_offset";

    // ---- shape geometry params ---- 
    inline constexpr const char* k_halfExtents       = "half_extents";
    inline constexpr const char* k_radius            = "radius";
    inline constexpr const char* k_halfHeight        = "half_height";
    inline constexpr const char* k_mesh              = "mesh";

    // ---- shape type enum values ----
    inline constexpr const char* k_shapeBox              = "box";
    inline constexpr const char* k_shapeSphere           = "sphere";
    inline constexpr const char* k_shapeCapsule          = "capsule";
    inline constexpr const char* k_shapePlane            = "plane";
    inline constexpr const char* k_shapeConvexMesh       = "convex_mesh";
    inline constexpr const char* k_shapeTriangleMesh     = "triangle_mesh";

    // ---- shape flag enum values ----
    inline constexpr const char* k_simulation           = "simulation";
    inline constexpr const char* k_simulation_only      = "simulation_only";
    inline constexpr const char* k_trigger              = "trigger";
    inline constexpr const char* k_trigger_only         = "trigger_only";
    inline constexpr const char* k_query_only           = "query_only";


    // ----------------------------
    //  MaterialDef
    // ----------------------------

    inline constexpr const char* k_staticFriction       = "static_friction";
    inline constexpr const char* k_dynamicFriction      = "dynamic_friction";
    inline constexpr const char* k_restitution          = "restitution";


    // ----------------------------
    //  SimFilter / QryFilter
    // ----------------------------

    inline constexpr const char* k_word0                = "word0";
    inline constexpr const char* k_word1                = "word1";
    inline constexpr const char* k_word2                = "word2";
    inline constexpr const char* k_word3                = "word3";


    // ----------------------------
    //  MeshDef
    // ----------------------------

    inline constexpr const char* k_src                  = "src";
    inline constexpr const char* k_meshIndex            = "mesh_index";
    inline constexpr const char* k_primitiveIndex       = "primitive_index";
    inline constexpr const char* k_cooked               = "cooked";


    // ----------------------------
    //  DynBodyDef
    // ----------------------------

    inline constexpr const char* k_density              = "density";
    inline constexpr const char* k_linearDamping        = "linear_damping";
    inline constexpr const char* k_angularDamping       = "angular_damping";
    inline constexpr const char* k_linearVelocity       = "linear_velocity";
    inline constexpr const char* k_angularVelocity      = "angular_velocity";


    // ----------------------------
    //  CCTDef
    // ----------------------------

    inline constexpr const char* k_cct_radius               = "radius";
    inline constexpr const char* k_cct_height               = "height";
    inline constexpr const char* k_cct_material             = "material";
    inline constexpr const char* k_cct_density              = "density";
    inline constexpr const char* k_cct_policy               = "policy";
    inline constexpr const char* k_cct_slopeLimit           = "slope_limit";
    inline constexpr const char* k_cct_invisibleWallHeight  = "invisible_wall_height";
    inline constexpr const char* k_cct_maxJumpHeight        = "max_jump_height";
    inline constexpr const char* k_cct_contactOffset        = "contact_offset";
    inline constexpr const char* k_cct_stepOffset           = "step_offset";
    inline constexpr const char* k_cct_scaleCoeff           = "scale_coeff";
    inline constexpr const char* k_cct_volumeGrowth         = "volume_growth";

    // ----------------------------
	//  MoveProfileDef
	// ----------------------------




    // ----------------------------
    //  Transform / Vec3 / Quat
    // ----------------------------

    inline constexpr const char* k_p = "p";
    inline constexpr const char* k_q = "q";

    inline constexpr int32 k_vec3Size = 3;
    inline constexpr int32 k_quatSize = 4;

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
        return json::json_pointer("/templates/" + std::to_string(templateIndex) + "/" + k_shapes + "/" + std::to_string(shapeIndex));
    }

    inline json::json_pointer MakeShapeFieldPtr(size_t templateIndex, size_t shapeIndex, const char* field)
    {
        return json::json_pointer("/templates/" + std::to_string(templateIndex) + "/" + k_shapes + "/" + std::to_string(shapeIndex) + "/" + field);
    }

    // materialDef 서브트리(주의: material은 보통 shape.material 아래에 객체로 존재)
    inline json::json_pointer MakeShapeMaterialFieldPtr(size_t templateIndex, size_t shapeIndex, const char* field)
    {
        return json::json_pointer("/templates/" + std::to_string(templateIndex) + "/" + k_shapes + "/" + std::to_string(shapeIndex) + "/" + k_material + "/" + field);
    }

    // meshDef 서브트리(주의: mesh는 보통 shape.mesh 아래에 객체로 존재)
    inline json::json_pointer MakeShapeMeshFieldPtr(size_t templateIndex, size_t shapeIndex, const char* field)
    {
        return json::json_pointer("/templates/" + std::to_string(templateIndex) + "/" + k_shapes + "/" + std::to_string(shapeIndex) + "/" + k_mesh + "/" + field);
    }

    inline json::json_pointer MakeDynBodyPtr(size_t templateIndex)
    {
        return MakeTemplateFieldPtr(templateIndex, k_dynBody);
    }

    inline json::json_pointer MakeDynBodyFieldPtr(size_t templateIndex, const char* field)
    {
        return json::json_pointer("/templates/" + std::to_string(templateIndex) + "/" + k_dynBody + "/" + field);
    }

    inline json::json_pointer MakeCctPtr(size_t templateIndex)
    {
        return MakeTemplateFieldPtr(templateIndex, k_cct);
    }

    inline json::json_pointer MakeCctFieldPtr(size_t templateIndex, const char* field)
    {
        return json::json_pointer("/templates/" + std::to_string(templateIndex) + "/" + k_cct + "/" + field);
    }

    inline json::json_pointer MakeCctMovementPtr(size_t templateIndex)
    {
        return json::json_pointer("/templates/" + std::to_string(templateIndex) + "/" + k_cct + "/" + kMovement);
    }

    inline json::json_pointer MakeCctMovementFieldPtr(size_t templateIndex, const char* field)
    {
        return json::json_pointer("/templates/" + std::to_string(templateIndex) + "/" + k_cct + "/" + kMovement + "/" + field);
    }


	class PhysicsPrefabIO
	{
	public:
		static json					LoadPrefabJsonFromFile(const std::string& path);
		static void					SavePrefabJsonToFile(const std::string& path, const json& prefab);

		static PhysicsAsset	        LoadPrefabAssetFromFile(const std::string& path);
		static PhysicsAsset	        LoadPrefabAssetFromJson(const json& root);

        static json                 LoadLevelJsonFromFile(const std::string& path);
        static void                 SaveLevelJsonToFile(const std::string& path, const json& level);

        static PhysicsLevelAsset     LoadLevelAssetFromFile(const std::string& path);
        static PhysicsLevelAsset     LoadLevelAssetFromJson(const json& root);

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
