#include "pch.h"
#include "jampx/prefab/PhysicsPrefabIO.h"

#include <fstream>
#include <stdexcept>

#include "jampx/PhysicsAsset.h"

namespace jam::px
{
	namespace detail
	{
		namespace k
		{
			inline constexpr const char* version					= "version";
			inline constexpr const char* materials					= "materials";
			inline constexpr const char* meshes						= "meshes";
			inline constexpr const char* shapes						= "shapes";
			inline constexpr const char* dyn_bodies					= "dyn_bodies";
			inline constexpr const char* cct_bodies					= "cct_bodies";
			inline constexpr const char* char_move_configs			= "char_move_configs";
			inline constexpr const char* kinematic_driver_configs	= "kinematic_driver_configs";
			inline constexpr const char* projectile_configs			= "projectile_configs";
			inline constexpr const char* templates					= "templates";


			// actor template
			inline constexpr const char* name						= "name";
			inline constexpr const char* type						= "type";
			inline constexpr const char* actor_type					= "actor_type";
			inline constexpr const char* body_type					= "body_type";
			inline constexpr const char* motion_type				= "motion_type";
			inline constexpr const char* motion_flags				= "motion_flags";
			inline constexpr const char* spawn_policy				= "spawn_policy";
			inline constexpr const char* allow_replication			= "allow_replication";
			inline constexpr const char* body						= "body";

			// actor types
			inline constexpr const char* generic					= "generic";
			inline constexpr const char* character					= "character";
			inline constexpr const char* projectile					= "projectile";

			// body types
			inline constexpr const char* rigid_body					= "rigid_body";
			inline constexpr const char* character_body				= "character_body";

			// motion types
			inline constexpr const char* none						= "none";
			inline constexpr const char* static_					= "static";
			inline constexpr const char* dynamic					= "dynamic";
			inline constexpr const char* kinematic					= "kinematic";
			inline constexpr const char* cct						= "cct";
			inline constexpr const char* remote_cct					= "remote_cct";

			// motion flags
			inline constexpr const char* disable_gravity			= "disable_gravity";
			inline constexpr const char* enable_ccd					= "enable_ccd";
			inline constexpr const char* lock_linear_x				= "lock_linear_x";
			inline constexpr const char* lock_linear_y				= "lock_linear_y";
			inline constexpr const char* lock_linear_z				= "lock_linear_z";
			inline constexpr const char* lock_angular_x				= "lock_angular_x";
			inline constexpr const char* lock_angular_y				= "lock_angular_y";
			inline constexpr const char* lock_angular_z				= "lock_angular_z";

			// spawn policy
			inline constexpr const char* level_only					= "level_only";
			inline constexpr const char* runtime_only				= "runtime_only";
			inline constexpr const char* both						= "both";


			// material properties
			inline constexpr const char* static_friction			= "static_friction";
			inline constexpr const char* dynamic_friction			= "dynamic_friction";
			inline constexpr const char* restitution				= "restitution";

			// shape types
			inline constexpr const char* box						= "box";
			inline constexpr const char* sphere						= "sphere";
			inline constexpr const char* capsule					= "capsule";
			inline constexpr const char* plane						= "plane";
			inline constexpr const char* triangle_mesh				= "triangle_mesh";
			inline constexpr const char* convex_mesh				= "convex_mesh";

			// shape flags
			inline constexpr const char* simulation					= "simulation";
			inline constexpr const char* simulation_only			= "simulation_only";
			inline constexpr const char* trigger					= "trigger";
			inline constexpr const char* trigger_only				= "trigger_only";
			inline constexpr const char* query_only					= "query_only";

			// shape definition
			inline constexpr const char* local_pose					= "local_pose";
			inline constexpr const char* material					= "material";
			inline constexpr const char* shape_flag					= "shape_flag";
			inline constexpr const char* sim_filter					= "sim_filter";
			inline constexpr const char* qry_filter					= "qry_filter";
			inline constexpr const char* contact_offset				= "contact_offset";
			inline constexpr const char* rest_offset				= "rest_offset";
			inline constexpr const char* half_extents				= "half_extents";
			inline constexpr const char* radius						= "radius";
			inline constexpr const char* half_height				= "half_height";
			inline constexpr const char* mesh						= "mesh";

			// mesh types
			inline constexpr const char* triangle					= "triangle";
			inline constexpr const char* convex						= "convex";
			
			// mesh definition
			inline constexpr const char* cooked_path				= "cooked_path";
			inline constexpr const char* src_path					= "src_path";
			inline constexpr const char* src_mesh_index				= "src_mesh_index";
			inline constexpr const char* src_primitive_index		= "src_primitive_index";

			// dynamic body definition
			inline constexpr const char* density					= "density";
			inline constexpr const char* linear_damping				= "linear_damping";
			inline constexpr const char* angular_damping			= "angular_damping";
			inline constexpr const char* linear_velocity			= "linear_velocity";
			inline constexpr const char* angular_velocity			= "angular_velocity";

			// cct body definition
			inline constexpr const char* height						= "height";
			inline constexpr const char* policy						= "policy";
			inline constexpr const char* slope_limit				= "slope_limit";
			inline constexpr const char* invisible_wall_height		= "invisible_wall_height";
			inline constexpr const char* max_jump_height			= "max_jump_height";
			inline constexpr const char* step_offset				= "step_offset";
			inline constexpr const char* scale_coeff				= "scale_coeff";
			inline constexpr const char* volume_growth				= "volume_growth";

			inline constexpr const char* controller_type			= "controller_type";
			inline constexpr const char* player						= "player";
			inline constexpr const char* ai							= "ai";
			inline constexpr const char* move_config				= "move_config";
			inline constexpr const char* hitboxes					= "hitboxes";

			inline constexpr const char* behavior					= "behavior";
			inline constexpr const char* kind						= "kind";
			inline constexpr const char* kinematic_driver			= "kinematic_driver";
			inline constexpr const char* config						= "config";

			inline constexpr const char* p							= "p";
			inline constexpr const char* q							= "q";
			inline constexpr const char* word0						= "word0";
			inline constexpr const char* word1						= "word1";
			inline constexpr const char* word2						= "word2";
			inline constexpr const char* word3						= "word3";

			// char move strict
			inline constexpr const char* gravity					= "gravity";
			inline constexpr const char* ground_accel				= "ground_accel";
			inline constexpr const char* ground_friction			= "ground_friction";
			inline constexpr const char* ground_max_speed			= "ground_max_speed";
			inline constexpr const char* air_accel					= "air_accel";
			inline constexpr const char* air_max_speed				= "air_max_speed";
			inline constexpr const char* cap_horizontal_only		= "cap_horizontal_only";
			inline constexpr const char* hard_speed_cap_air			= "hard_speed_cap_air";
			inline constexpr const char* soft_cap_start_air			= "soft_cap_start_air";
			inline constexpr const char* soft_cap_strength_air		= "soft_cap_strength_air";
			inline constexpr const char* stance						= "stance";
			inline constexpr const char* gait						= "gait";
			inline constexpr const char* jump						= "jump";
			inline constexpr const char* dash						= "dash";

			inline constexpr const char* standing_height			= "standing_height";
			inline constexpr const char* crouch_height				= "crouch_height";
			inline constexpr const char* crouch_speed_multiplier	= "crouch_speed_multiplier";
			inline constexpr const char* hold_to_crouch				= "hold_to_crouch";
			inline constexpr const char* prone_height				= "prone_height";
			inline constexpr const char* prone_speed_multiplier		= "prone_speed_multiplier";
			inline constexpr const char* hold_to_prone				= "hold_to_prone";

			inline constexpr const char* walk_speed_multiplier		= "walk_speed_multiplier";
			inline constexpr const char* run_speed_multiplier		= "run_speed_multiplier";
			inline constexpr const char* sprint_speed_multiplier	= "sprint_speed_multiplier";
			inline constexpr const char* sprint_accel_multiplier	= "sprint_accel_multiplier";
			inline constexpr const char* sprint_min_speed_to_start	= "sprint_min_speed_to_start";
			inline constexpr const char* sprint_allow_in_air		= "sprint_allow_in_air";

			inline constexpr const char* speed						= "speed";
			inline constexpr const char* coyote_time				= "coyote_time";
			inline constexpr const char* jump_buffer				= "jump_buffer";
			inline constexpr const char* edge_trigger				= "edge_trigger";
			inline constexpr const char* duration					= "duration";
			inline constexpr const char* override_locomotion		= "override_locomotion";
			inline constexpr const char* allow_in_air				= "allow_in_air";
			inline constexpr const char* end_on_collision			= "end_on_collision";
			inline constexpr const char* steer_factor				= "steer_factor";

			inline constexpr const char* common						= "common";
			inline constexpr const char* source						= "source";
			inline constexpr const char* source_type				= "source_type";

			inline constexpr const char* compute_derived_vel		= "compute_derived_vel";
			inline constexpr const char* carry_riders				= "carry_riders";
			inline constexpr const char* sweep						= "sweep";
			inline constexpr const char* max_speed					= "max_speed";

			inline constexpr const char* waypoint					= "waypoint";
			inline constexpr const char* curve						= "curve";
			inline constexpr const char* orbit						= "orbit";
			inline constexpr const char* follow						= "follow";
			inline constexpr const char* network_pose				= "network_pose";

			inline constexpr const char* waypoints					= "waypoints";
			inline constexpr const char* pause_duration				= "pause_duration";
			inline constexpr const char* loop_mode					= "loop_mode";
			inline constexpr const char* once						= "once";
			inline constexpr const char* ping_pong					= "ping_pong";
			inline constexpr const char* loop						= "loop";
			inline constexpr const char* use_ease_profile			= "use_ease_profile";
			inline constexpr const char* ease_type					= "ease_type";
			inline constexpr const char* ease_profile				= "ease_profile";
			inline constexpr const char* ease_in_time				= "ease_in_time";
			inline constexpr const char* ease_out_time				= "ease_out_time";

			inline constexpr const char* linear						= "linear";
			inline constexpr const char* smooth_step				= "smooth_step";
			inline constexpr const char* smoother_step				= "smoother_step";
			inline constexpr const char* in_sine					= "in_sine";
			inline constexpr const char* out_sine					= "out_sine";
			inline constexpr const char* in_out_sine				= "in_out_sine";
			inline constexpr const char* in_quad					= "in_quad";
			inline constexpr const char* out_quad					= "out_quad";
			inline constexpr const char* in_out_quad				= "in_out_quad";
			inline constexpr const char* in_cubic					= "in_cubic";
			inline constexpr const char* out_cubic					= "out_cubic";
			inline constexpr const char* in_out_cubic				= "in_out_cubic";

			inline constexpr const char* control_points				= "control_points";
			inline constexpr const char* build_segments				= "build_segments";
			inline constexpr const char* alpha						= "alpha";
			inline constexpr const char* degree						= "degree";
			inline constexpr const char* catmull_rom				= "catmull_rom";
			inline constexpr const char* b_spline					= "b_spline";
			inline constexpr const char* bezier						= "bezier";
						
			inline constexpr const char* center_mode				= "center_mode";
			inline constexpr const char* fixed_center				= "fixed_center";
			inline constexpr const char* target_offset				= "target_offset";
			inline constexpr const char* fixed_point				= "fixed_point";
			inline constexpr const char* follow_target				= "follow_target";
			inline constexpr const char* plane_mode					= "plane_mode";
			inline constexpr const char* custom_plane_normal		= "custom_plane_normal";
			inline constexpr const char* xy							= "xy";
			inline constexpr const char* xz							= "xz";
			inline constexpr const char* yz							= "yz";
			inline constexpr const char* custom						= "custom";
			inline constexpr const char* radius_mode				= "radius_mode";
			inline constexpr const char* circle						= "circle";
			inline constexpr const char* ellipse					= "ellipse";
			inline constexpr const char* ellipse_radius				= "ellipse_radius";

			inline constexpr const char* initial_angle_rad			= "initial_angle_rad";
			inline constexpr const char* angular_speed_rad			= "angular_speed_rad";
			inline constexpr const char* end_mode					= "end_mode";
			inline constexpr const char* clamp						= "clamp";
			inline constexpr const char* min_angle_rad				= "min_angle_rad";
			inline constexpr const char* max_angle_rad				= "max_angle_rad";
			inline constexpr const char* orientation_mode			= "orientation_mode";
			inline constexpr const char* keep_rotation				= "keep_rotation";
			inline constexpr const char* face_center				= "face_center";
			inline constexpr const char* orient_along_velocity		= "orient_along_velocity";
			inline constexpr const char* initial_rotation			= "initial_rotation";
			inline constexpr const char* use_ease_at_ends			= "use_ease_at_ends";
			inline constexpr const char* end_ease_profile			= "end_ease_profile";
			inline constexpr const char* compute_derived_velocity	= "compute_derived_velocity";

			inline constexpr const char* target_id					= "target_id";
			inline constexpr const char* offset						= "offset";
			inline constexpr const char* offset_space				= "offset_space";
			inline constexpr const char* target_local				= "target_local";
			inline constexpr const char* world						= "world";
			inline constexpr const char* position_follow_speed		= "position_follow_speed";
			inline constexpr const char* rotation_follow_speed		= "rotation_follow_speed";
			inline constexpr const char* max_linear_speed			= "max_linear_speed";
			inline constexpr const char* max_angular_speed			= "max_angular_speed";
			inline constexpr const char* rotation_mode				= "rotation_mode";
			inline constexpr const char* keep_world_rotation		= "keep_world_rotation";
			inline constexpr const char* match_target_rotation		= "match_target_rotation";
			inline constexpr const char* look_at_target				= "look_at_target";
			inline constexpr const char* snap_if_target_missing		= "snap_if_target_missing";
			inline constexpr const char* keep_last_pose_if_missing	= "keep_last_pose_if_missing";

			// projectile config sections
			inline constexpr const char* motion						= "motion";
			inline constexpr const char* hit						= "hit";
			inline constexpr const char* lifetime					= "lifetime";
			inline constexpr const char* homing						= "homing";

			// projectile common
			inline constexpr const char* model						= "model";
			inline constexpr const char* kind_projectile			= "kind";

			// projectile kind
			inline constexpr const char* dyn_sim					= "dyn_sim";
			inline constexpr const char* analytic					= "analytic";
			inline constexpr const char* hitscan					= "hitscan";

			// projectile motion model
			inline constexpr const char* ballistic					= "ballistic";
			inline constexpr const char* homing_steer				= "homing_steer";
			inline constexpr const char* homing_lead				= "homing_lead";
			inline constexpr const char* homing_pn					= "homing_pn";

			// projectile motion
			inline constexpr const char* initial_velocity			= "initial_velocity";
			inline constexpr const char* gravity_scale				= "gravity_scale";

			// projectile hit
			inline constexpr const char* request_fd					= "request_fd";
			inline constexpr const char* use_shape_sweep			= "use_shape_sweep";
			inline constexpr const char* fallback_raycast			= "fallback_raycast";
			inline constexpr const char* raycast_fallback			= "raycast_fallback";
			inline constexpr const char* shape_sweep				= "shape_sweep";
			inline constexpr const char* sphere_sweep				= "sphere_sweep";
			inline constexpr const char* expanding_shape_sweep		= "expanding_shape_sweep";
			inline constexpr const char* expanding_sphere_sweep		= "expanding_sphere_sweep";

			// projectile homing
			inline constexpr const char* acceleration				= "acceleration";
			inline constexpr const char* max_turn_rate				= "max_turn_rate";
			inline constexpr const char* enable_homing				= "enable_homing";
			inline constexpr const char* keep_speed_constant		= "keep_speed_constant";
			inline constexpr const char* reacquire_target			= "reacquire_target";
			inline constexpr const char* keep_last_direction		= "keep_last_direction";
			inline constexpr const char* lead_time_scale			= "lead_time_scale";
			inline constexpr const char* max_predict_time			= "max_predict_time";
			inline constexpr const char* navigation_gain			= "navigation_gain";
			inline constexpr const char* max_lateral_accel			= "max_lateral_accel";

			// projectile lifetime
			inline constexpr const char* max_range					= "max_range";
			inline constexpr const char* max_lifetime				= "max_lifetime";



			inline constexpr const char* scene_name					= "scene_name";
			inline constexpr const char* layers						= "layers";
			inline constexpr const char* enabled					= "enabled";
			inline constexpr const char* instances					= "instances";

			inline constexpr const char* level_actor_id				= "level_actor_id";
			inline constexpr const char* template_name				= "template";
			inline constexpr const char* pose						= "pose";
			inline constexpr const char* overrides					= "overrides";
		}

		static PxVec3 ParseVec3(const json& j)
		{
			if (!j.is_array() || j.size() != 3) 
				throw std::runtime_error("vec3 must be [x,y,z]");
			return PxVec3{ j[0].get<float>(), j[1].get<float>(), j[2].get<float>() };
		}

		static PxQuat ParseQuat(const json& j)
		{
			if (!j.is_array() || j.size() != 4) 
				throw std::runtime_error("quat must be [x,y,z,w]");
			PxQuat q{ j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>() };
			q.normalize();
			return q;
		}

		static PxTransform ParseTransform(const json& j)
		{
			return PxTransform{ ParseVec3(j.at(k::p)), ParseQuat(j.at(k::q)) };
		}

		template<class THandle>
		static THandle ParseHandleRef(const json& j)
		{
			if (j.is_number_unsigned())
				return THandle::FromU32(j.get<uint32>());

			if (j.is_string())
				return THandle::FromU32(fnv1a<uint32>(j.get<std::string_view>()));

			throw std::runtime_error("handle_ref must be string or u32");
		}

		template<class THandle>
		static THandle ParseMapKeyHandle(const std::string& key)
		{
			bool isNum = !key.empty() && std::ranges::all_of(key, [](char c) { return c >= '0' && c <= '9'; });
			if (isNum) return THandle::FromU32(static_cast<uint32>(std::stoul(key)));
			return THandle::FromU32(fnv1a<uint32>(key));
		}

		static eActorType ParseActorType(std::string_view s)
		{
			if (s == k::generic)	return eActorType::Generic;
			if (s == k::projectile)	return eActorType::Projectile;
			if (s == k::character)	return eActorType::Character;
			return eActorType::None;
		}

		static eBodyType ParseBodyType(std::string_view s)
		{
			if (s == k::rigid_body)		return eBodyType::Rigid;
			if (s == k::character_body)	return eBodyType::Character;
			throw std::runtime_error("invalid body_type");
		}

		static eMotionType ParseMotionType(std::string_view s)
		{
			if (s == k::none)			return eMotionType::None;
			if (s == k::static_)		return eMotionType::Static;
			if (s == k::dynamic)		return eMotionType::Dynamic;
			if (s == k::kinematic)		return eMotionType::Kinematic;
			if (s == k::cct)			return eMotionType::CCT;
			if (s == k::remote_cct)		return eMotionType::RemoteCCT;
			throw std::runtime_error("invalid motion_type");
		}

		static eSpawnPolicy ParseSpawnPolicy(std::string_view s)
		{
			if (s == k::level_only)		return eSpawnPolicy::LevelOnly;
			if (s == k::runtime_only)	return eSpawnPolicy::RuntimeOnly;
			if (s == k::both)			return eSpawnPolicy::Both;
			throw std::runtime_error("invalid spawn_policy");
		}

		static eShapeType ParseShapeType(std::string_view s)
		{
			if (s == k::none)			return eShapeType::None;
			if (s == k::box)			return eShapeType::Box;
			if (s == k::sphere)			return eShapeType::Sphere;
			if (s == k::capsule)		return eShapeType::Capsule;
			if (s == k::plane)			return eShapeType::Plane;
			if (s == k::triangle_mesh)	return eShapeType::TriangleMesh;
			if (s == k::convex_mesh)	return eShapeType::ConvexMesh;
			throw std::runtime_error("invalid shape.type");
		}

		static eShapeFlag ParseShapeFlag(std::string_view s)
		{
			if (s == k::simulation)			return eShapeFlag::Simulation;
			if (s == k::simulation_only)	return eShapeFlag::SimulationOnly;
			if (s == k::trigger)			return eShapeFlag::Trigger;
			if (s == k::trigger_only)		return eShapeFlag::TriggerOnly;
			if (s == k::query_only)			return eShapeFlag::QueryOnly;
			throw std::runtime_error("invalid shape_flag");
		}

		static MotionFlag::Flags ParseMotionFlags(const json& arr)
		{
			MotionFlag::Flags out = MotionFlag::NONE;
			if (!arr.is_array()) return out;

			for (const auto& v : arr)
			{
				const std::string s = v.get<std::string>();
				if		(s == k::disable_gravity)	out |= MotionFlag::DISABLE_GRAVITY;
				else if (s == k::enable_ccd)		out |= MotionFlag::ENABLE_CCD;
				else if (s == k::lock_linear_x)		out |= MotionFlag::LOCK_LINEAR_X;
				else if (s == k::lock_linear_y)		out |= MotionFlag::LOCK_LINEAR_Y;
				else if (s == k::lock_linear_z)		out |= MotionFlag::LOCK_LINEAR_Z;
				else if (s == k::lock_angular_x)	out |= MotionFlag::LOCK_ANGULAR_X;
				else if (s == k::lock_angular_y)	out |= MotionFlag::LOCK_ANGULAR_Y;
				else if (s == k::lock_angular_z)	out |= MotionFlag::LOCK_ANGULAR_Z;
			}
			return out;
		}

		static SimFD ParseSimFD(const json& j)
		{
			PxFilterData fd{};
			fd.word0 = j.at(k::word0).get<PxU32>();
			fd.word1 = j.at(k::word1).get<PxU32>();
			fd.word2 = j.at(k::word2).get<PxU32>();
			fd.word3 = j.at(k::word3).get<PxU32>();
			return SimFD::FromPx(fd);
		}

		static QueryFD ParseQueryFD(const json& j)
		{
			PxFilterData fd{};
			fd.word0 = j.at(k::word0).get<PxU32>();
			fd.word1 = j.at(k::word1).get<PxU32>();
			fd.word2 = j.at(k::word2).get<PxU32>();
			fd.word3 = j.at(k::word3).get<PxU32>();
			return QueryFD::FromPx(fd);
		}

		static CharacterMoveConfig ParseCharacterMoveConfig(const json& j)
		{
			CharacterMoveConfig c{};

			c.gravity				= j.value(k::gravity, c.gravity);
			c.groundAccel			= j.value(k::ground_accel, c.groundAccel);
			c.groundFriction		= j.value(k::ground_friction, c.groundFriction);
			c.groundMaxSpeed		= j.value(k::ground_max_speed, c.groundMaxSpeed);
			c.airAccel				= j.value(k::air_accel, c.airAccel);
			c.airMaxSpeed			= j.value(k::air_max_speed, c.airMaxSpeed);
			c.capHorizontalOnly		= j.value(k::cap_horizontal_only, c.capHorizontalOnly);
			c.hardSpeedCapAir		= j.value(k::hard_speed_cap_air, c.hardSpeedCapAir);
			c.softCapStartAir		= j.value(k::soft_cap_start_air, c.softCapStartAir);
			c.softCapStrengthAir	= j.value(k::soft_cap_strength_air, c.softCapStrengthAir);

			if (j.contains(k::stance))
			{
				const auto& s = j.at(k::stance);
				c.stance.standingHeight			= s.value(k::standing_height, c.stance.standingHeight);
				c.stance.crouchHeight			= s.value(k::crouch_height, c.stance.crouchHeight);
				c.stance.crouchSpeedMultiplier	= s.value(k::crouch_speed_multiplier, c.stance.crouchSpeedMultiplier);
				c.stance.holdToCrouch			= s.value(k::hold_to_crouch, c.stance.holdToCrouch);
				c.stance.proneHeight			= s.value(k::prone_height, c.stance.proneHeight);
				c.stance.proneSpeedMultiplier	= s.value(k::prone_speed_multiplier, c.stance.proneSpeedMultiplier);
				c.stance.holdToProne			= s.value(k::hold_to_prone, c.stance.holdToProne);
			}

			if (j.contains(k::gait))
			{
				const auto& g = j.at(k::gait);
				c.gait.walkSpeedMultiplier		= g.value(k::walk_speed_multiplier, c.gait.walkSpeedMultiplier);
				c.gait.runSpeedMultiplier		= g.value(k::run_speed_multiplier, c.gait.runSpeedMultiplier);
				c.gait.sprintSpeedMultiplier	= g.value(k::sprint_speed_multiplier, c.gait.sprintSpeedMultiplier);
				c.gait.sprintAccelMultiplier	= g.value(k::sprint_accel_multiplier, c.gait.sprintAccelMultiplier);
				c.gait.sprintMinSpeedToStart	= g.value(k::sprint_min_speed_to_start, c.gait.sprintMinSpeedToStart);
				c.gait.sprintAllowInAir			= g.value(k::sprint_allow_in_air, c.gait.sprintAllowInAir);
			}

			if (j.contains(k::jump))
			{
				const auto& jp = j.at(k::jump);
				c.jump.speed		= jp.value(k::speed, c.jump.speed);
				c.jump.coyoteTime	= jp.value(k::coyote_time, c.jump.coyoteTime);
				c.jump.jumpBuffer	= jp.value(k::jump_buffer, c.jump.jumpBuffer);
				c.jump.edgeTrigger	= jp.value(k::edge_trigger, c.jump.edgeTrigger);
			}

			if (j.contains(k::dash))
			{
				const auto& d = j.at(k::dash);
				c.dash.speed				= d.value(k::speed, c.dash.speed);
				c.dash.duration				= d.value(k::duration, c.dash.duration);
				c.dash.overrideLocomotion	= d.value(k::override_locomotion, c.dash.overrideLocomotion);
				c.dash.allowInAir			= d.value(k::allow_in_air, c.dash.allowInAir);
				c.dash.endOnCollision		= d.value(k::end_on_collision, c.dash.endOnCollision);
				c.dash.steerFactor			= d.value(k::steer_factor, c.dash.steerFactor);
			}

			return c;
		}

		static eEaseType ParseEaseType(std::string_view s)
		{
			if (s == k::linear)			return eEaseType::Linear;
			if (s == k::smooth_step)	return eEaseType::SmoothStep;
			if (s == k::smoother_step)	return eEaseType::SmootherStep;
			if (s == k::in_sine)		return eEaseType::InSine;
			if (s == k::out_sine)		return eEaseType::OutSine;
			if (s == k::in_out_sine)	return eEaseType::InOutSine;
			if (s == k::in_quad)		return eEaseType::InQuad;
			if (s == k::out_quad)		return eEaseType::OutQuad;
			if (s == k::in_out_quad)	return eEaseType::InOutQuad;
			if (s == k::in_cubic)		return eEaseType::InCubic;
			if (s == k::out_cubic)		return eEaseType::OutCubic;
			if (s == k::in_out_cubic)	return eEaseType::InOutCubic;
			throw std::runtime_error("invalid ease_type");
		}

		static EaseProfile ParseEaseProfile(const json& j)
		{
			EaseProfile p{};
			p.easeInTime  = j.value(k::ease_in_time, p.easeInTime);
			p.easeOutTime = j.value(k::ease_out_time, p.easeOutTime);
			return p;
		}

		static eCurveType ParseCurveType(std::string_view s)
		{
			if (s == k::catmull_rom)	return eCurveType::CatmullRom;
			if (s == k::b_spline)		return eCurveType::BSpline;
			if (s == k::bezier)			return eCurveType::Bezier;
			throw std::runtime_error("invalid curve type");
		}

		static eWaypointLoop ParseWaypointLoop(std::string_view s)
		{
			if (s == k::once)		return eWaypointLoop::Once;
			if (s == k::loop)		return eWaypointLoop::Loop;
			if (s == k::ping_pong)	return eWaypointLoop::PingPong;
			throw std::runtime_error("invalid waypoint loop");
		}

		static eOrbitPlaneMode ParseOrbitPlaneMode(std::string_view s)
		{
			if (s == k::xy)		return eOrbitPlaneMode::XY;
			if (s == k::xz)		return eOrbitPlaneMode::XZ;
			if (s == k::yz)		return eOrbitPlaneMode::YZ;
			if (s == k::custom) return eOrbitPlaneMode::Custom;
			throw std::runtime_error("invalid orbit plane mode");
		}

		static eOrbitCenterMode ParseOrbitCenterMode(std::string_view s)
		{
			if (s == k::fixed_point)	return eOrbitCenterMode::FixedPoint;
			if (s == k::follow_target)	return eOrbitCenterMode::FollowTarget;
			throw std::runtime_error("invalid orbit center mode");
		}

		static eOrbitRadiusMode ParseOrbitRadiusMode(std::string_view s)
		{
			if (s == k::circle)		return eOrbitRadiusMode::Circle;
			if (s == k::ellipse)	return eOrbitRadiusMode::Ellipse;
			throw std::runtime_error("invalid orbit radius mode");
		}

		static eOrbitOrientationMode ParseOrbitOrientationMode(std::string_view s)
		{
			if (s == k::keep_rotation)			return eOrbitOrientationMode::KeepRotation;
			if (s == k::face_center)			return eOrbitOrientationMode::FaceCenter;
			if (s == k::orient_along_velocity)	return eOrbitOrientationMode::OrientAlongVelocity;
			throw std::runtime_error("invalid orbit orientation mode");
		}

		static eOrbitEndMode ParseOrbitEndMode(std::string_view s)
		{
			if (s == k::loop)		return eOrbitEndMode::Loop;
			if (s == k::ping_pong)	return eOrbitEndMode::PingPong;
			if (s == k::clamp)		return eOrbitEndMode::Clamp;
			throw std::runtime_error("invalid orbit end mode");
		}

		static eFollowOffsetSpace ParseFollowOffsetSpace(std::string_view s)
		{
			if (s == k::target_local)	return eFollowOffsetSpace::TargetLocal;
			if (s == k::world)			return eFollowOffsetSpace::World;
			throw std::runtime_error("invalid follow offset space");
		}

		static eFollowRotationMode ParseFollowRotationMode(std::string_view s)
		{
			if (s == k::keep_world_rotation)	return eFollowRotationMode::KeepWorldRotation;
			if (s == k::match_target_rotation)	return eFollowRotationMode::MatchTargetRotation;
			if (s == k::look_at_target)			return eFollowRotationMode::LookAtTarget;
			if (s == k::orient_along_velocity)	return eFollowRotationMode::OrientAlongVelocity;
			throw std::runtime_error("invalid follow rotation mode");
		}

		static KinematicDriverConfig ParseKinematicDriverConfig(const json& j)
		{
			KinematicDriverConfig cfg{};

			if (j.contains(k::common))
			{
				const auto& c = j.at(k::common);
				cfg.common.computeDerivedVel = c.value(k::compute_derived_vel, cfg.common.computeDerivedVel);
				cfg.common.carryRiders		 = c.value(k::carry_riders, cfg.common.carryRiders);
				cfg.common.sweep			 = c.value(k::sweep, cfg.common.sweep);
				cfg.common.maxSpeed			 = c.value(k::max_speed, cfg.common.maxSpeed);
			}

			const auto& s = j.at(k::source);
			const std::string st = s.at(k::source_type).get<std::string>();

			if (st == k::waypoint)
			{
				WaypointSource src{};
				for (const auto& wj : s.at(k::waypoints))
				{
					KinematicWaypoint w{};
					w.pose			= ParseTransform(wj.at(k::pose));
					w.pauseDuration = wj.value(k::pause_duration, w.pauseDuration);
					src.waypoints.push_back(w);
				}
				src.speed			= s.value(k::speed, src.speed);
				src.loopMode		= ParseWaypointLoop(s.value(k::loop_mode, std::string(k::loop)));
				src.useEaseProfile	= s.value(k::use_ease_profile, src.useEaseProfile);
				src.easeType		= ParseEaseType(s.value(k::ease_type, std::string(k::linear)));
				if (s.contains(k::ease_profile)) src.easeProfile = ParseEaseProfile(s.at(k::ease_profile));

				cfg.source = std::move(src);
				return cfg;
			}

			if (st == k::curve)
			{
				CurveSource src{};
				for (const auto& p : s.at(k::control_points)) src.controlPoints.push_back(ParseVec3(p));
				src.type			= ParseCurveType(s.value(k::type, std::string(k::catmull_rom)));
				src.speed			= s.value(k::speed, src.speed);
				src.duration		= s.value(k::duration, src.duration);
				src.loop			= s.value(k::loop, src.loop);
				src.buildSegments	= s.value(k::build_segments, src.buildSegments);
				src.useEaseProfile	= s.value(k::use_ease_profile, src.useEaseProfile);
				src.easeType		= ParseEaseType(s.value(k::ease_type, std::string(k::smooth_step)));
				if (s.contains(k::ease_profile)) src.easeProfile = ParseEaseProfile(s.at(k::ease_profile));
				src.alpha			= s.value(k::alpha, src.alpha);
				src.degree			= s.value(k::degree, src.degree);

				cfg.source = std::move(src);
				return cfg;
			}

			if (st == k::orbit)
			{
				OrbitSource src{};
				src.centerMode = ParseOrbitCenterMode(s.value(k::center_mode, std::string(k::fixed_point)));
				if (s.contains(k::fixed_center))  src.fixedCenter  = ParseVec3(s.at(k::fixed_center));
				if (s.contains(k::target_offset)) src.targetOffset = ParseVec3(s.at(k::target_offset));

				src.planeMode = ParseOrbitPlaneMode(s.value(k::plane_mode, std::string(k::xz)));
				if (s.contains(k::custom_plane_normal)) src.customPlaneNormal = ParseVec3(s.at(k::custom_plane_normal));

				src.radiusMode = ParseOrbitRadiusMode(s.value(k::radius_mode, std::string(k::circle)));
				src.radius	   = s.value(k::radius, src.radius);
				if (s.contains(k::ellipse_radius))
				{
					const auto& er = s.at(k::ellipse_radius);
					src.ellipseRadius = PxVec2(er.at(0).get<float>(), er.at(1).get<float>());
				}

				src.initialAngleRad = s.value(k::initial_angle_rad, src.initialAngleRad);
				src.angularSpeedRad = s.value(k::angular_speed_rad, src.angularSpeedRad);
				src.endMode			= ParseOrbitEndMode(s.value(k::end_mode, std::string(k::loop)));
				src.minAngleRad		= s.value(k::min_angle_rad, src.minAngleRad);
				src.maxAngleRad		= s.value(k::max_angle_rad, src.maxAngleRad);

				src.orientationMode = ParseOrbitOrientationMode(s.value(k::orientation_mode, std::string(k::orient_along_velocity)));
				if (s.contains(k::initial_rotation)) src.initialRotation = ParseQuat(s.at(k::initial_rotation));
				src.useEaseAtEnds = s.value(k::use_ease_at_ends, src.useEaseAtEnds);
				if (s.contains(k::end_ease_profile)) src.endEaseProfile = ParseEaseProfile(s.at(k::end_ease_profile));
				src.computeDerivedVelocity = s.value(k::compute_derived_velocity, src.computeDerivedVelocity);

				cfg.source = std::move(src);
				return cfg;
			}

			if (st == k::follow)
			{
				FollowSource src{};
				src.targetId				= s.at(k::target_id).get<ObjectId>();
				if (s.contains(k::offset)) src.offset = ParseVec3(s.at(k::offset));
				src.offsetSpace				= ParseFollowOffsetSpace(s.value(k::offset_space, std::string(k::target_local)));
				src.positionFollowSpeed		= s.value(k::position_follow_speed, src.positionFollowSpeed);
				src.rotationFollowSpeed		= s.value(k::rotation_follow_speed, src.rotationFollowSpeed);
				src.maxLinearSpeed			= s.value(k::max_linear_speed, src.maxLinearSpeed);
				src.maxAngularSpeed			= s.value(k::max_angular_speed, src.maxAngularSpeed);
				src.rotationMode			= ParseFollowRotationMode(s.value(k::rotation_mode, std::string(k::keep_world_rotation)));
				src.snapIfTargetMissing		= s.value(k::snap_if_target_missing, src.snapIfTargetMissing);
				src.keepLastPoseIfMissing	= s.value(k::keep_last_pose_if_missing, src.keepLastPoseIfMissing);
				src.computeDerivedVelocity	= s.value(k::compute_derived_velocity, src.computeDerivedVelocity);

				cfg.source = std::move(src);
				return cfg;
			}

			if (st == k::network_pose)
			{
				NetworkPoseSource src{};
				src.computeDerivedVelocity = s.value(k::compute_derived_velocity, src.computeDerivedVelocity);
				cfg.source = src;
				return cfg;
			}

			throw std::runtime_error("unsupported kinematic source_type");
		}

		static eProjectileKind ParseProjectileKind(std::string_view s)
		{
			if (s == k::dyn_sim)  return eProjectileKind::DYN_SIM;
			if (s == k::analytic) return eProjectileKind::ANALYTIC;
			if (s == k::hitscan)  return eProjectileKind::HITSCAN;
			throw std::runtime_error("invalid projectile kind");
		}

		static eProjectileMotionModel ParseProjectileMotionModel(std::string_view s)
		{
			if (s == k::linear)       return eProjectileMotionModel::Linear;
			if (s == k::ballistic)    return eProjectileMotionModel::Ballistic;
			if (s == k::homing_steer) return eProjectileMotionModel::HomingSteer;
			if (s == k::homing_lead)  return eProjectileMotionModel::HomingLead;
			if (s == k::homing_pn)    return eProjectileMotionModel::HomingPN;
			throw std::runtime_error("invalid projectile motion model");
		}

		static eProjectileHitModel ParseProjectileHitModel(std::string_view s)
		{
			if (s == k::raycast_fallback)		return eProjectileHitModel::RaycastFallback;
			if (s == k::shape_sweep)			return eProjectileHitModel::ShapeSweep;
			if (s == k::sphere_sweep)			return eProjectileHitModel::SphereSweep;
			if (s == k::expanding_shape_sweep)	return eProjectileHitModel::ExpandingShapeSweep;
			if (s == k::expanding_sphere_sweep)	return eProjectileHitModel::ExpandingSphereSweep;
			throw std::runtime_error("invalid projectile hit model");
		}

		static RequestQueryFD ParseRequestQueryFD(const json& j)
		{
			PxFilterData fd{};
			fd.word0 = j.at(k::word0).get<PxU32>();
			fd.word1 = j.at(k::word1).get<PxU32>();
			fd.word2 = j.at(k::word2).get<PxU32>();
			fd.word3 = j.at(k::word3).get<PxU32>();
			return RequestQueryFD::FromPx(fd);
		}

		static ProjectileConfig ParseProjectileConfig(const json& j)
		{
			ProjectileConfig cfg{};

			cfg.kind = ParseProjectileKind(j.value(k::kind_projectile, std::string(k::analytic)));

			if (j.contains(k::motion))
			{
				const auto& m = j.at(k::motion);
				cfg.motion.model		= ParseProjectileMotionModel(m.value(k::model, std::string(k::ballistic)));
				if (m.contains(k::initial_velocity)) cfg.motion.initialVelocity = ParseVec3(m.at(k::initial_velocity));
				cfg.motion.gravityScale = m.value(k::gravity_scale, cfg.motion.gravityScale);
			}

			if (j.contains(k::hit))
			{
				const auto& h = j.at(k::hit);
				cfg.hit.model			= ParseProjectileHitModel(h.value(k::model, std::string(k::shape_sweep)));
				cfg.hit.useShapeSweep	= h.value(k::use_shape_sweep, cfg.hit.useShapeSweep);
				cfg.hit.fallbackRaycast = h.value(k::fallback_raycast, cfg.hit.fallbackRaycast);
				if (h.contains(k::request_fd))
					cfg.hit.requestFd = ParseRequestQueryFD(h.at(k::request_fd));
			}

			if (j.contains(k::lifetime))
			{
				const auto& l = j.at(k::lifetime);
				cfg.lifetime.maxRange	 = l.value(k::max_range, cfg.lifetime.maxRange);
				cfg.lifetime.maxLifetime = l.value(k::max_lifetime, cfg.lifetime.maxLifetime);
			}

			if (j.contains(k::homing))
			{
				const auto& h = j.at(k::homing);
				cfg.homing.targetId				= h.value(k::target_id, cfg.homing.targetId);
				cfg.homing.maxSpeed				= h.value(k::max_speed, cfg.homing.maxSpeed);
				cfg.homing.acceleration			= h.value(k::acceleration, cfg.homing.acceleration);
				cfg.homing.maxTurnRate			= h.value(k::max_turn_rate, cfg.homing.maxTurnRate);
				cfg.homing.enableHoming			= h.value(k::enable_homing, cfg.homing.enableHoming);
				cfg.homing.keepSpeedConstant	= h.value(k::keep_speed_constant, cfg.homing.keepSpeedConstant);
				cfg.homing.reacquireTarget		= h.value(k::reacquire_target, cfg.homing.reacquireTarget);
				cfg.homing.keepLastDirection	= h.value(k::keep_last_direction, cfg.homing.keepLastDirection);
				cfg.homing.leadTimeScale		= h.value(k::lead_time_scale, cfg.homing.leadTimeScale);
				cfg.homing.maxPredictTime		= h.value(k::max_predict_time, cfg.homing.maxPredictTime);
				cfg.homing.navigationGain		= h.value(k::navigation_gain, cfg.homing.navigationGain);
				cfg.homing.maxLateralAccel		= h.value(k::max_lateral_accel, cfg.homing.maxLateralAccel);
			}

			return cfg;
		}

		static RigidSpawnOverrides ParseRigidOverrides(const json& ov)
		{
			RigidSpawnOverrides out{};

			if (ov.contains(k::linear_velocity))
			{
				const PxVec3 v = ParseVec3(ov.at(k::linear_velocity));
				out.mask |= SpawnOverrideMask::LINEAR_VEL;
				out.linearVelocity = Vec3{ v.x, v.y, v.z };
			}
			if (ov.contains(k::angular_velocity))
			{
				const PxVec3 v = ParseVec3(ov.at(k::angular_velocity));
				out.mask |= SpawnOverrideMask::ANGULAR_VEL;
				out.angularVelocity = Vec3{ v.x, v.y, v.z };
			}
			if (ov.contains(k::linear_damping))
			{
				out.mask |= SpawnOverrideMask::LINEAR_DAMP;
				out.linearDamping = ov.at(k::linear_damping).get<float>();
			}
			if (ov.contains(k::angular_damping))
			{
				out.mask |= SpawnOverrideMask::ANGULAR_DAMP;
				out.angularDamping = ov.at(k::angular_damping).get<float>();
			}

			return out;
		}
	}

	json PhysicsPrefabIO::LoadPrefabJsonFromFile(const std::string& path)
	{
		std::ifstream ifs(path);
		if (!ifs.is_open()) throw std::runtime_error("failed to open prefab: " + path);

		json prefab;
		ifs >> prefab;

#if JAMPX_WITH_EDITOR
		ValidatePrefabJson(prefab);
#endif
		return prefab;
	}

	void PhysicsPrefabIO::SavePrefabJsonToFile(const std::string& path, const json& prefab)
	{
#if JAMPX_WITH_EDITOR
		ValidatePrefabJson(prefab);
#endif
		std::ofstream ofs(path);
		if (!ofs.is_open()) throw std::runtime_error("failed to open prefab for write: " + path);
		ofs << prefab.dump(2);
	}

	PhysicsAsset PhysicsPrefabIO::LoadPrefabAssetFromFile(const std::string& path)
	{
		return LoadPrefabAssetFromJson(LoadPrefabJsonFromFile(path));
	}

	PhysicsAsset PhysicsPrefabIO::LoadPrefabAssetFromJson(const json& root)
	{
		PhysicsAsset asset{};
		asset.version = root.at(detail::k::version).get<int32>();
		if (asset.version != 1)
			throw std::runtime_error("Unsupported physics asset version");

		// materials
		if (root.contains(detail::k::materials))
		{
			for (const auto& [k, v] : root.at(detail::k::materials).items())
			{
				MaterialDef m{};
				m.name				= v.value(detail::k::name, k);
				m.staticFriction	= v.at(detail::k::static_friction).get<float>();
				m.dynamicFriction	= v.at(detail::k::dynamic_friction).get<float>();
				m.restitution		= v.at(detail::k::restitution).get<float>();

				const MaterialHandle h = detail::ParseMapKeyHandle<MaterialHandle>(k);
				asset.materials[h] = std::move(m);
			}
		}

		// meshes
		if (root.contains(detail::k::meshes))
		{
			for (const auto& [k, v] : root.at(detail::k::meshes).items())
			{
				MeshDef m{};
				const std::string t = v.at(detail::k::type).get<std::string>();
				m.type				= (t == detail::k::convex) ? eMeshType::Convex : eMeshType::Triangle;
				m.cookedPath		= v.at(detail::k::cooked_path).get<std::string>();
				m.srcPath			= v.value(detail::k::src_path, "");
				m.srcMeshIndex		= v.value(detail::k::src_mesh_index, 0);
				m.srcPrimitiveIndex = v.value(detail::k::src_primitive_index, 0);

				const MeshHandle h = detail::ParseMapKeyHandle<MeshHandle>(k);
				asset.meshes[h] = std::move(m);
			}
		}

		// shapes
		if (root.contains(detail::k::shapes))
		{
			for (const auto& [k, v] : root.at(detail::k::shapes).items())
			{
				ShapeDef s{};
				s.type			= detail::ParseShapeType(v.at(detail::k::type).get<std::string>());
				s.localPose		= detail::ParseTransform(v.at(detail::k::local_pose));
				s.material		= detail::ParseHandleRef<MaterialHandle>(v.at(detail::k::material));
				s.shapeFlag		= detail::ParseShapeFlag(v.at(detail::k::shape_flag).get<std::string>());
				s.simFD			= detail::ParseSimFD(v.at(detail::k::sim_filter));
				s.qryFD			= detail::ParseQueryFD(v.at(detail::k::qry_filter));
				s.contactOffset = v.value(detail::k::contact_offset, s.contactOffset);
				s.restOffset	= v.value(detail::k::rest_offset, s.restOffset);

				if (v.contains(detail::k::half_extents)) s.halfExtents	= detail::ParseVec3(v.at(detail::k::half_extents));
				if (v.contains(detail::k::radius))		 s.radius		= v.at(detail::k::radius).get<float>();
				if (v.contains(detail::k::half_height))	 s.halfHeight	= v.at(detail::k::half_height).get<float>();
				if (v.contains(detail::k::mesh))			 s.mesh			= detail::ParseHandleRef<MeshHandle>(v.at(detail::k::mesh));

				const ShapeHandle h = detail::ParseMapKeyHandle<ShapeHandle>(k);
				asset.shapes[h] = std::move(s);
			}
		}

		// dyn bodies
		if (root.contains(detail::k::dyn_bodies))
		{
			for (const auto& [k, v] : root.at(detail::k::dyn_bodies).items())
			{
				DynamicBodyDef d{};
				d.density		 = v.value(detail::k::density, d.density);
				d.linearDamping  = v.value(detail::k::linear_damping, d.linearDamping);
				d.angularDamping = v.value(detail::k::angular_damping, d.angularDamping);
				if (v.contains(detail::k::linear_velocity))  d.linearVelocity  = detail::ParseVec3(v.at(detail::k::linear_velocity));
				if (v.contains(detail::k::angular_velocity)) d.angularVelocity = detail::ParseVec3(v.at(detail::k::angular_velocity));

				const DynamicBodyHandle h = detail::ParseMapKeyHandle<DynamicBodyHandle>(k);
				asset.dynBodies[h] = std::move(d);
			}
		}

		// cct bodies
		if (root.contains(detail::k::cct_bodies))
		{
			for (const auto& [k, v] : root.at(detail::k::cct_bodies).items())
			{
				CCTBodyDef c{};
				c.radius				= v.value(detail::k::radius, c.radius);
				c.height				= v.value(detail::k::height, c.height);
				c.material				= detail::ParseHandleRef<MaterialHandle>(v.at(detail::k::material));
				c.density				= v.value(detail::k::density, c.density);
				c.slopeLimit			= v.value(detail::k::slope_limit, c.slopeLimit);
				c.invisibleWallHeight	= v.value(detail::k::invisible_wall_height, c.invisibleWallHeight);
				c.maxJumpHeight			= v.value(detail::k::max_jump_height, c.maxJumpHeight);
				c.contactOffset			= v.value(detail::k::contact_offset, c.contactOffset);
				c.stepOffset			= v.value(detail::k::step_offset, c.stepOffset);
				c.scaleCoeff			= v.value(detail::k::scale_coeff, c.scaleCoeff);
				c.volumeGrowth			= v.value(detail::k::volume_growth, c.volumeGrowth);

				const CCTBodyHandle h = detail::ParseMapKeyHandle<CCTBodyHandle>(k);
				asset.cctBodies[h] = c;
			}
		}

		// char move configs
		if (root.contains(detail::k::char_move_configs))
		{
			for (const auto& [k, v] : root.at(detail::k::char_move_configs).items())
			{
				const CharacterMoveConfigHandle h = detail::ParseMapKeyHandle<CharacterMoveConfigHandle>(k);
				asset.charMoveConfigs[h] = detail::ParseCharacterMoveConfig(v);
			}
		}

		// kinematic_driver_configs
		if (root.contains(detail::k::kinematic_driver_configs))
		{
			for (const auto& [k, v] : root.at(detail::k::kinematic_driver_configs).items())
			{
				const KinematicDriverConfigHandle h = detail::ParseMapKeyHandle<KinematicDriverConfigHandle>(k);
				asset.kinematicDriverConfigs[h] = detail::ParseKinematicDriverConfig(v);
			}
		}

		// projectile_configs
		if (root.contains(detail::k::projectile_configs))
		{
			for (const auto& [k, v] : root.at(detail::k::projectile_configs).items())
			{
				const ProjectileConfigHandle h = detail::ParseMapKeyHandle<ProjectileConfigHandle>(k);
				asset.projectileConfigs[h] = detail::ParseProjectileConfig(v);
			}
		}

		// templates
		for (const auto& [k, v] : root.at(detail::k::templates).items())
		{
			ActorTemplateDef t{};
			t.name				= v.value(detail::k::name, k);
			t.actorType			= detail::ParseActorType(v.at(detail::k::actor_type).get<std::string>());
			t.bodyType			= detail::ParseBodyType(v.at(detail::k::body_type).get<std::string>());
			t.motionType		= detail::ParseMotionType(v.at(detail::k::motion_type).get<std::string>());
			t.motionFlags		= detail::ParseMotionFlags(v.value(detail::k::motion_flags, json::array()));
			t.spawnPolicy		= detail::ParseSpawnPolicy(v.value(detail::k::spawn_policy, detail::k::both));
			t.allowReplication  = v.value(detail::k::allow_replication, true);

			const auto& b = v.at(detail::k::body);

			if (t.bodyType == eBodyType::Rigid)
			{
				RigidBodyDef rb{};
				for (const auto& sh : b.at(detail::k::shapes))
					rb.shapes.push_back(detail::ParseHandleRef<ShapeHandle>(sh));

				if (b.contains(detail::k::dynamic))
					rb.dynamic = detail::ParseHandleRef<DynamicBodyHandle>(b.at(detail::k::dynamic));

				if (b.contains(detail::k::behavior))
				{
					const auto& bj = b.at(detail::k::behavior);
					const std::string kind = bj.at(detail::k::kind).get<std::string>();
					if (kind == detail::k::kinematic_driver && bj.contains(detail::k::config))
						rb.behavior = detail::ParseHandleRef<KinematicDriverConfigHandle>(bj.at(detail::k::config));
					else if (kind == detail::k::projectile && bj.contains(detail::k::config))
						rb.behavior = detail::ParseHandleRef<ProjectileConfigHandle>(bj.at(detail::k::config));
				}

				t.body = std::move(rb);
			}
			else
			{
				CharacterBodyDef cb{};
				cb.cct = detail::ParseHandleRef<CCTBodyHandle>(b.at(detail::k::cct));

				if (b.contains(detail::k::hitboxes))
					for (const auto& hb : b.at(detail::k::hitboxes))
						cb.hitboxes.push_back(detail::ParseHandleRef<ShapeHandle>(hb));

				const std::string ct = b.value(detail::k::controller_type, detail::k::player);
				if		(ct == detail::k::ai)	cb.controllerType = eCharacterControlType::AI;
				else if (ct == detail::k::none) cb.controllerType = eCharacterControlType::None;
				else							cb.controllerType = eCharacterControlType::Player;

				if (b.contains(detail::k::move_config))
					cb.moveConfig = detail::ParseHandleRef<CharacterMoveConfigHandle>(b.at(detail::k::move_config));

				t.body = std::move(cb);
			}

			const TemplateHandle th = detail::ParseMapKeyHandle<TemplateHandle>(k);
			asset.templates[th] = std::move(t);
		}

		return asset;
	}

	json PhysicsPrefabIO::LoadLevelJsonFromFile(const std::string& path)
	{
		std::ifstream ifs(path);
		if (!ifs.is_open())
			throw std::runtime_error("PrefabIO::LoadLevelJsonFromFile - failed to open: " + path);

		json level;
		ifs >> level;

#if JAMPX_WITH_EDITOR
		ValidateLevelJson(level);
#endif

		return level;
	}

	void PhysicsPrefabIO::SaveLevelJsonToFile(const std::string& path, const json& level)
	{
#if JAMPX_WITH_EDITOR
		ValidateLevelJson(level);
#endif

		std::ofstream ofs(path);
		if (!ofs.is_open())
			throw std::runtime_error("PhysicsPrefabIO::SaveLevelJsonToFile - failed to open: " + path);

		ofs << level.dump(2);
	}

	PhysicsLevelAsset PhysicsPrefabIO::LoadLevelAssetFromFile(const std::string& path)
	{
		return LoadLevelAssetFromJson(LoadLevelJsonFromFile(path));
	}

	PhysicsLevelAsset PhysicsPrefabIO::LoadLevelAssetFromJson(const json& root)
	{
		PhysicsLevelAsset out{};
		out.version = root.at(detail::k::version).get<int32>();

		if (out.version != 1)
			throw std::runtime_error("Unsupported level version (expected 2)");

		out.sceneName = root.value(detail::k::scene_name, std::string{});

		const json& layers = root.at(detail::k::layers);
		if (!layers.is_array())
			throw std::runtime_error("level.layers must be array");

		out.layers.reserve(layers.size());

		for (const json& lj : layers)
		{
			PhysicsLevelLayerDef layer{};
			layer.name	  = lj.at(detail::k::name).get<std::string>();
			layer.enabled = lj.value(detail::k::enabled, true);

			const json& insts = lj.at(detail::k::instances);
			if (!insts.is_array())
				throw std::runtime_error("level.layers[].instances must be array");

			layer.instances.reserve(insts.size());

			for (const json& ij : insts)
			{
				PhysicsLevelInstanceDef inst{};
				inst.levelActorId = ij.value(detail::k::level_actor_id, 0u);
				inst.templateName = ij.at(detail::k::template_name).get<std::string>();
				inst.pose		  = detail::ParseTransform(ij.at(detail::k::pose));

				if (ij.contains(detail::k::overrides))
					inst.overrides = detail::ParseRigidOverrides(ij.at(detail::k::overrides));

				layer.instances.push_back(std::move(inst));
			}

			out.layers.push_back(std::move(layer));
		}

		return out;
	}

#if JAMPX_WITH_EDITOR
	const char* PhysicsPrefabIO::PrefabSchemaPath()
	{
		return JAMPX_PREFAB_SCHEMA_PATH;
	}

	const json& PhysicsPrefabIO::PrefabSchemaJson()
	{
		static json schema = [] {
				std::ifstream schemaStream(PrefabSchemaPath());
				if (!schemaStream.is_open())
					throw std::runtime_error(std::string("failed to open prefab-schema: ") + PrefabSchemaPath());

				json s;
				schemaStream >> s;
				return s;
			}();

		return schema;
	}

	void PhysicsPrefabIO::ValidatePrefabJson(const json& prefab)
	{
		static json_validator validator = [] {
				json_validator v;
				v.set_root_schema(PrefabSchemaJson());
				return v;
			}();

		(void)validator.validate(prefab);
	}

	const char* PhysicsPrefabIO::LevelSchemaPath()
	{
		return JAMPX_LEVEL_SCHEMA_PATH;
	}

	const json& PhysicsPrefabIO::LevelSchemaJson()
	{
		static json schema = [] {
				std::ifstream schemaStream(LevelSchemaPath());
				if (!schemaStream.is_open())
					throw std::runtime_error(std::string("failed to open level-schema: ") + LevelSchemaPath());

				json s;
				schemaStream >> s;
				return s;
			}();

		return schema;
	}

	void PhysicsPrefabIO::ValidateLevelJson(const json& level)
	{
		static json_validator validator = [] {
				json_validator v;
				v.set_root_schema(LevelSchemaJson());
				return v;
			}();

		(void)validator.validate(level);
	}
#endif


}
