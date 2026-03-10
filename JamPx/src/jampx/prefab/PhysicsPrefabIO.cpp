#include "pch.h"
#include "jampx/prefab/PhysicsPrefabIO.h"

#include <fstream>
#include <stdexcept>

#include "jampx/PhysicsAsset.h"

namespace jam::px
{
	namespace
	{
		// ----- physx::PxVec3 -----

		PxVec3 ParseVec3(const json& j)
		{
			if (!j.is_array() || j.size() != k_vec3Size)
				throw std::runtime_error("vec3 must be array[3] [x, y, z]");

			return { j[0].get<float>(), j[1].get<float>(), j[2].get<float>() };
		}

		PxVec3 ParseVec3(const json& j, const char* key)
		{
			if (!j.contains(key))
				return PxVec3{physx::PxZero };

			return ParseVec3(j.at(key));
		}


		// ----- physx::PxQuat -----

		PxQuat ParseQuat(const json& j)
		{
			if (!j.is_array() || j.size() != k_quatSize)
				throw std::runtime_error("quat must be array[4] [x, y, z, w]");

			PxQuat q{ j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>() };
			q.normalize();
			return q;
		}

		PxQuat ParseQuat(const json& j, const char* key)
		{
			if (!j.contains(key))
				return PxQuat{ physx::PxIdentity };

			return ParseQuat(j.at(key));
		}


		// ----- physx::PxTransform -----

		PxTransform ParseTransform(const json& j)
		{
			if (!j.is_object())
				throw std::runtime_error("transform must be object");

			PxTransform tf{ physx::PxIdentity };
			tf.p = ParseVec3(j.at(k_p));
			tf.q = ParseQuat(j.at(k_q));

			return tf;
		}

		PxTransform ParseTransform(const json& j, const char* key)
		{
			if (!j.contains(key))
				return PxTransform{ physx::PxIdentity };

			return ParseTransform(j.at(key));
		}

		eActorType ParseActorType(const json& j)
		{
			const std::string s = j[k_actorType].get<std::string>();

			if (s == k_genericActor)	return eActorType::Generic;
			if (s == k_projectile)		return eActorType::Projectile;
			if (s == k_character)		return eActorType::Character;

			return eActorType::None;
		}

		eMotionType ParseMotionType(const json& j)
		{
			const std::string s = j[k_motionType].get<std::string>();

			if (s == k_static)		return eMotionType::Static;
			if (s == k_dynamic)		return eMotionType::Dynamic;
			if (s == k_kinematic)	return eMotionType::Kinematic;

			return eMotionType::None;
		}

		MotionFlag::Flags ParseBodyFlag(const json& j)
		{
			const uint32 flags = j[k_bodyFlags].get<uint32>();

			return MotionFlag::Flags(flags);
		}

		eSpawnPolicy ParseSpawnPolicy(const json& j)
		{
			const std::string s = j[k_spawnPolicy].get<std::string>();

			if (s == k_spawnLevelOnly)	  return eSpawnPolicy::LevelOnly;
			if (s == k_spawnRuntimeOnly)  return eSpawnPolicy::RuntimeOnly;
			if (s == k_spawnBoth)		  return eSpawnPolicy::Both;

			throw std::runtime_error("template.spawn_policy must be one of: level_only / runtime_only / both");
		}

		MaterialDef ParseMaterialDef(const json& j)
		{
			MaterialDef def{};
			def.staticFriction	= j[k_staticFriction].get<float>();
			def.dynamicFriction = j[k_dynamicFriction].get<float>();
			def.restitution		= j[k_restitution].get<float>();
			return def;
		}

		MaterialHandle InternMaterial(INOUT PhysicsAsset& asset, const MaterialDef& def)
		{
			const MaterialHandle handle = jam::Fnv1aHandleOf<MaterialHandle>(def);

			auto it = asset.materials.find(handle);
			if (it == asset.materials.end())
			{
				asset.materials.emplace(handle, def);
			}
			else
			{
				if (it->second != def)
					throw std::runtime_error("material handle hash collision (different material values)");
			}

			return handle;
		}

		MeshDef ParseMeshDef(const json& j)
		{
			MeshDef def{};
			def.cookedPath			= j[k_cooked].get<std::string>();
			def.srcPath				= j.value(k_src, "");
			def.srcMeshIndex		= j.value(k_meshIndex, 0);
			def.srcPrimitiveIndex	= j.value(k_primitiveIndex, 0);

			return def;
		}

		MeshHandle InternMesh(INOUT PhysicsAsset& asset, const MeshDef& def)
		{
			const MeshHandle handle = jam::Fnv1aHandleOf<MeshHandle>(def);

			auto it = asset.meshes.find(handle);
			if (it == asset.meshes.end())
			{
				asset.meshes.emplace(handle, def);
			}
			else
			{
				if (it->second != def) throw std::runtime_error("mesh handle hash collision (different mesh values)");
			}

			return handle;
		}

		eShapeType ParseShapeType(const json& j)
		{
			const std::string s = j[k_shapeType].get<std::string>();

			if (s == k_shapeBox)			return eShapeType::Box;
			if (s == k_shapeSphere)			return eShapeType::Sphere;
			if (s == k_shapeCapsule)		return eShapeType::Capsule;
			if (s == k_shapePlane)			return eShapeType::Plane;
			if (s == k_shapeTriangleMesh)	return eShapeType::TriangleMesh;
			if (s == k_shapeConvexMesh)		return eShapeType::ConvexMesh;

			throw std::runtime_error("shape.type is invalid");
		}

		eShapeFlag ParseShapeFlag(const json& j)
		{
			const std::string s = j[k_shapeFlag].get<std::string>();

			if (s == k_simulation)			return eShapeFlag::Simulation;
			if (s == k_simulation_only)		return eShapeFlag::SimulationOnly;
			if (s == k_trigger)				return eShapeFlag::Trigger;
			if (s == k_trigger_only)		return eShapeFlag::TriggerOnly;
			if (s == k_query_only)			return eShapeFlag::QueryOnly;

			throw std::runtime_error("shape.shapeFlag must be one of: simulation/simulation_only/trigger/trigger_only/query_only");
		}

		SimFD ParseSimFD(const json& j)
		{
			const json& v = j.at(k_simFilter);

			PxFilterData fd{};
			fd.word0 = v[k_word0].get<PxU32>();
			fd.word1 = v[k_word1].get<PxU32>();
			fd.word2 = v[k_word2].get<PxU32>();
			fd.word3 = v[k_word3].get<PxU32>();

			return SimFD::FromPx(fd);
		}

		QueryFD ParseQueryFD(const json& j)
		{
			const json& v = j.at(k_qryFilter);

			PxFilterData fd{};
			fd.word0 = v[k_word0].get<PxU32>();
			fd.word1 = v[k_word1].get<PxU32>();
			fd.word2 = v[k_word2].get<PxU32>();
			fd.word3 = v[k_word3].get<PxU32>();

			return QueryFD::FromPx(fd);
		}

		ShapeHandle InternShape(INOUT PhysicsAsset& asset, const ShapeDef& def)
		{
			const ShapeHandle handle = jam::Fnv1aHandleOf<ShapeHandle>(def);

			auto it = asset.shapes.find(handle);
			if (it == asset.shapes.end())
			{
				asset.shapes.emplace(handle, def);
			}
			else if (jam::HashOf<uint32>(it->second) != jam::HashOf<uint32>(def))
			{
				throw std::runtime_error("shape handle hash collision (different shape defs)");
			}

			return handle;
		}

		ShapeDef ParseShapeDef(const json& j, INOUT PhysicsAsset& asset)
		{
			ShapeDef s{};
			s.type = ParseShapeType(j);

			{
				const MaterialDef mat = ParseMaterialDef(j.at(k_material));
				s.material = InternMaterial(asset, mat);
			}

			s.localPose     = ParseTransform(j.at(k_localPose));
			s.shapeFlag     = ParseShapeFlag(j);
			s.simFD		    = ParseSimFD(j);
			s.qryFD		    = ParseQueryFD(j);
			s.contactOffset = j.value(k_contactOffset, s.contactOffset);
			s.restOffset    = j.value(k_restOffset, s.restOffset);

			switch (s.type)
			{
			case eShapeType::Box:
				s.halfExtents	= ParseVec3(j, k_halfExtents);
				break;
			case eShapeType::Sphere:
				s.radius		= j[k_radius].get<float>();
				break;
			case eShapeType::Capsule:
				s.radius		= j[k_radius].get<float>();
				s.halfHeight	= j[k_halfHeight].get<float>();
				break;
			case eShapeType::Plane:
				break;

			case eShapeType::TriangleMesh:
			case eShapeType::ConvexMesh:
			{
				const MeshDef mesh = ParseMeshDef(j.at(k_mesh));
				s.mesh = InternMesh(asset, mesh);
			}
			break;

			default:
				throw std::runtime_error("unsupported shape type");
			}

			return s;
		}

		DynamicBodyDef ParseDynamicBodyDef(const json& body)
		{
			DynamicBodyDef out{};
			out.density			= body[k_density].get<float>();
			out.linearDamping	= body[k_linearDamping].get<float>();
			out.angularDamping	= body[k_angularDamping].get<float>();
			out.linearVelocity	= ParseVec3(body, k_linearVelocity);
			out.angularVelocity = ParseVec3(body, k_angularVelocity);
			return out;
		}



		CCTBodyDef ParseCCTDef(const json& cj, INOUT PhysicsAsset& asset)
		{
			CCTBodyDef out{};
			out.radius			= cj.value(k_cct_radius, out.radius);
			out.height			= cj.value(k_cct_height, out.height);

			{
				const MaterialDef mat = ParseMaterialDef(cj.at(k_material));
				out.material = InternMaterial(asset, mat);
			}

			out.contactOffset	= cj.value(k_cct_contactOffset, out.contactOffset);
			out.stepOffset		= cj.value(k_cct_stepOffset, out.stepOffset);
			out.slopeLimit		= cj.value(k_cct_slopeLimit, out.slopeLimit);



			return out;
		}
	}


	json PhysicsPrefabIO::LoadPrefabJsonFromFile(const std::string& path)
	{
		std::ifstream ifs(path);
		if (!ifs.is_open())
			throw std::runtime_error("PrefabIO::LoadPrefabJsonFromFile - failed to open: " + path);

		json prefab;
		ifs >> prefab;

#if JAMPX_WITH_EDITOR
		ValidatePrefabJson(prefab);
#endif

		return prefab;
	}

	void PhysicsPrefabIO::SavePrefabJsonToFile(const std::string& path, const json& prefab)
	{
		std::ofstream ofs(path);
		if (!ofs.is_open())
			throw std::runtime_error("PhysicsPrefabIO::SavePrefabJsonToFile - failed to open: " + path);

#if JAMPX_WITH_EDITOR
		ValidatePrefabJson(prefab);
#endif

		ofs << prefab.dump(2);
	}

	PhysicsAsset PhysicsPrefabIO::LoadPrefabAssetFromFile(const std::string& path)
	{
		const json root = LoadPrefabJsonFromFile(path);
		return LoadPrefabAssetFromJson(root);
	}

	PhysicsAsset PhysicsPrefabIO::LoadPrefabAssetFromJson(const json& root)
	{
		PhysicsAsset asset{};
		asset.version = root[k_version].get<int32>();
		if (asset.version != 1)
			throw std::runtime_error("Unsupported prefab version");

		for (const json& t : root[k_templates])
		{
			ActorTemplateDef out{};
			out.name				= t[k_name].get<std::string>();
			out.actorType			= ParseActorType(t);
			out.motionType			= ParseMotionType(t);
			out.motionFlags			= ParseBodyFlag(t);
			out.spawnPolicy			= ParseSpawnPolicy(t);
			out.allowReplication	= t[k_allowReplication].get<bool>();

			if (out.motionType == eMotionType::Dynamic || out.motionType == eMotionType::Kinematic)
			{
				out.dynamic = ParseDynamicBodyDef(t.at(k_dynBody));
			}

			if (out.actorType == eActorType::Character)
			{
				const json& cj = t.value(k_cct, json::object());
				if (!cj.empty())
					out.cct = ParseCCTDef(cj, asset);

				if (out.cct.hasHitbox && !t.at(k_shapes).empty())
					throw std::runtime_error("character template: cct.has_hitbox=true requires template.shapes to be empty");
			}

			for (const json& sj : t[k_shapes])
			{
				ShapeDef s = ParseShapeDef(sj, asset);
				const ShapeHandle sh = InternShape(asset, s);
				out.shapes.push_back(sh);
			}

			const TemplateHandle th = jam::Fnv1aHandleOf<TemplateHandle>(out);
			asset.templates.emplace(th, std::move(out));
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
		std::ofstream ofs(path);
		if (!ofs.is_open())
			throw std::runtime_error("PhysicsPrefabIO::SavePrefabJsonToFile - failed to open: " + path);

#if JAMPX_WITH_EDITOR
		ValidateLevelJson(level);
#endif

		ofs << level.dump(2);
	}

	PhysicsLevelAsset PhysicsPrefabIO::LoadLevelAssetFromFile(const std::string& path)
	{
		const json root = LoadLevelJsonFromFile(path);
		return LoadLevelAssetFromJson(root);
	}

	PhysicsLevelAsset PhysicsPrefabIO::LoadLevelAssetFromJson(const json& root)
	{
		PhysicsLevelAsset out{};
		out.version = root.at(k_version).get<int32>();
		if (out.version != 1)
			throw std::runtime_error("Unsupported level version");

		const json& insts = root.at("instances");
		if (!insts.is_array())
			throw std::runtime_error("level.instances must be array");

		out.instances.reserve(insts.size());

		for (const json& ij : insts)
		{
			PhysicsLevelInstanceDef inst{};
			inst.templateName	= ij["template"].get<std::string>();
			inst.pose			= ParseTransform(ij, "pose");

			if (!ij.contains("overrides"))
			{
				out.instances.push_back(std::move(inst));
				continue;
			}

			const json& ov = ij.at("overrides");

			if (ov.contains(k_linearVelocity))
				inst.overrides.linearVelocity = ParseVec3(ov.at(k_linearVelocity));
			if (ov.contains(k_angularVelocity))
				inst.overrides.angularVelocity = ParseVec3(ov.at(k_angularVelocity));
			if (ov.contains(k_linearDamping))
				inst.overrides.linearDamping = ov[k_linearDamping].get<float>();
			if (ov.contains(k_angularDamping))
				inst.overrides.angularDamping = ov[k_angularDamping].get<float>();

			out.instances.push_back(std::move(inst));
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
