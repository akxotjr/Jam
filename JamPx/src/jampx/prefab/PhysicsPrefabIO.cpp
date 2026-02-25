#include "pch.h"
#include "jampx/prefab/PhysicsPrefabIO.h"

#include <fstream>
#include <stdexcept>

#include "jampx/prefab/PrefabAssets.h"

namespace jam::px::prefab
{
	namespace
	{
		// ----- physx::PxVec3 -----

		static PxVec3 ParseVec3(const json& j)
		{
			if (!j.is_array() || j.size() != kVec3Size)
				throw std::runtime_error("vec3 must be array[3] [x, y, z]");

			return { j[0].get<float>(), j[1].get<float>(), j[2].get<float>() };
		}

		static PxVec3 ParseVec3(const json& j, const char* key)
		{
			if (!j.contains(key))
				return PxVec3{ PxZero };

			return ParseVec3(j.at(key));
		}


		// ----- physx::PxQuat -----

		static PxQuat ParseQuat(const json& j)
		{
			if (!j.is_array() || j.size() != kQuatSize)
				throw std::runtime_error("quat must be array[4] [x, y, z, w]");

			PxQuat q{ j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>() };
			q.normalize();
			return q;
		}

		static PxQuat ParseQuat(const json& j, const char* key)
		{
			if (!j.contains(key))
				return PxQuat{ PxIdentity };

			return ParseQuat(j.at(key));
		}


		// ----- physx::PxTransform -----

		static PxTransform ParseTransform(const json& j)
		{
			if (!j.is_object())
				throw std::runtime_error("transform must be object");

			PxTransform tf{ PxIdentity };
			tf.p = ParseVec3(j.at(kP));
			tf.q = ParseQuat(j.at(kQ));

			return tf;
		}

		static PxTransform ParseTransform(const json& j, const char* key)
		{
			if (!j.contains(key))
				return PxTransform{ PxIdentity };

			return ParseTransform(j.at(key));
		}




		static ePrefabSpawnPolicy ParseSpawnPolicy(const json& j)
		{
			const std::string s = j[kSpawnPolicy].get<string>();

			if (s == vSpawnLevelOnly)	  return ePrefabSpawnPolicy::LEVEL_ONLY;
			if (s == vSpawnRuntimeOnly)   return ePrefabSpawnPolicy::RUNTIME_ONLY;
			if (s == vSpawnBoth)		  return ePrefabSpawnPolicy::BOTH;

			throw std::runtime_error("template.spawn_policy must be one of: level_only / runtime_only / both");
		}

		static ePrefabBodyKind ParseBodyKind(const json& j)
		{
			const std::string s = j[kKind].get<string>();

			if (s == vKindStatic)	 return ePrefabBodyKind::STATIC;
			if (s == vKindDynamic)	 return ePrefabBodyKind::DYNAMIC;
			if (s == vKindKinematic) return ePrefabBodyKind::KINEMATIC;
			if (s == vKindCharacter) return ePrefabBodyKind::CHARACTER;

			throw std::runtime_error("body.kind must be one of: static / dynamic / kinematic / character");
		}

		static PrefabMaterialDef ParseMaterialDef(const json& j)
		{
			PrefabMaterialDef def{};
			def.staticFriction	= j[kStaticFriction].get<float>();
			def.dynamicFriction = j[kDynamicFriction].get<float>();
			def.restitution		= j[kRestitution].get<float>();
			return def;
		}

		static MaterialHandle InternMaterial(INOUT PhysicsPrefabAsset& asset, const PrefabMaterialDef& def)
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

		static PrefabMeshDef ParseMeshDef(const json& j)
		{
			PrefabMeshDef def{};
			def.cookedPath				= j[kCooked].get<string>();
			def.srcGltfPath				= j.value(kSrc, "");
			def.srcGltfMeshIndex		= j.value(kMeshIndex, 0);
			def.srcGltfPrimitiveIndex	= j.value(kPrimitiveIndex, 0);

			return def;
		}

		static MeshHandle InternMesh(INOUT PhysicsPrefabAsset& asset, const PrefabMeshDef& def)
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

		static eShapeType ParseShapeType(const json& j)
		{
			const string s = j[kType].get<string>();
			if (s == vShapeBox)				return eShapeType::BOX;
			if (s == vShapeSphere)			return eShapeType::SPHERE;
			if (s == vShapeCapsule)			return eShapeType::CAPSULE;
			if (s == vShapePlane)			return eShapeType::PLANE;
			if (s == vShapeTriangleMesh)	return eShapeType::TRIANGLE_MESH;
			if (s == vShapeConvexMesh)		return eShapeType::CONVEX_MESH;
			if (s == vShapeHeightField)		return eShapeType::HEIGHT_FIELD;

			throw std::runtime_error("shape.type is invalid");
		}

		static eShapeFlag ParseShapeFlag(const json& j)
		{
			const string s = j[kShapeFlag].get<string>();
			if (s == vShapeFlagSimulation)		return eShapeFlag::SIMULATION;
			if (s == vShapeFlagSimulationOnly) return eShapeFlag::SIMULATION_ONLY;
			if (s == vShapeFlagTrigger)		return eShapeFlag::TRIGGER;
			if (s == vShapeFlagTriggerOnly)	return eShapeFlag::TRIGGER_ONLY;
			if (s == vShapeFlagQueryOnly)		return eShapeFlag::QUERY_ONLY;

			throw std::runtime_error("shape.shapeFlag must be one of: simulation/simulation_only/trigger/trigger_only/query_only");
		}

		static SimFD ParseSimFD(const json& j)
		{
			const json& v = j.at(kSimFilter);

			PxFilterData fd{};
			fd.word0 = v[kWord0].get<PxU32>();
			fd.word1 = v[kWord1].get<PxU32>();
			fd.word2 = v[kWord2].get<PxU32>();
			fd.word3 = v[kWord3].get<PxU32>();

			return SimFD::FromPx(fd);
		}

		static QueryFD ParseQueryFD(const json& j)
		{
			const json& v = j.at(kQryFilter);

			PxFilterData fd{};
			fd.word0 = v[kWord0].get<PxU32>();
			fd.word1 = v[kWord1].get<PxU32>();
			fd.word2 = v[kWord2].get<PxU32>();
			fd.word3 = v[kWord3].get<PxU32>();

			return QueryFD::FromPx(fd);
		}

		static ShapeHandle InternShape(INOUT PhysicsPrefabAsset& asset, const PrefabShapeDef& def)
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

		static PrefabShapeDef ParseShapeDef(const json& j, INOUT PhysicsPrefabAsset& asset)
		{
			PrefabShapeDef s{};
			s.type = ParseShapeType(j);

			{
				const PrefabMaterialDef mat = ParseMaterialDef(j.at(kMaterial));
				s.material = InternMaterial(asset, mat);
			}

			s.localPose     = ParseTransform(j.at(kLocalPose));
			s.shapeFlag     = ParseShapeFlag(j);
			s.simFD		    = ParseSimFD(j);
			s.qryFD		    = ParseQueryFD(j);
			s.contactOffset = j.value(kContactOffset, s.contactOffset);
			s.restOffset    = j.value(kRestOffset, s.restOffset);

			switch (s.type)
			{
			case eShapeType::BOX:
				s.boxHalfExtents	= ParseVec3(j, kHalfExtents);
				break;
			case eShapeType::SPHERE:
				s.sphereRadius		= j[kRadius].get<float>();
				break;
			case eShapeType::CAPSULE:
				s.capsuleRadius		= j[kRadius].get<float>();
				s.capsuleHalfHeight = j[kHalfHeight].get<float>();
				break;
			case eShapeType::PLANE:
				break;

			case eShapeType::TRIANGLE_MESH:
			case eShapeType::CONVEX_MESH:
			case eShapeType::HEIGHT_FIELD:
			{
				const PrefabMeshDef mesh = ParseMeshDef(j.at(kMesh));
				s.mesh = InternMesh(asset, mesh);
			}
			break;

			default:
				throw std::runtime_error("unsupported shape type");
			}

			return s;
		}

		static PrefabDynamicBodyDef ParseDynamicBodyDef(const json& body)
		{
			PrefabDynamicBodyDef out{};
			out.density			= body[kDensity].get<float>();
			out.useGravity		= body[kUseGravity].get<bool>();
			out.linearDamping	= body[kLinearDamping].get<float>();
			out.angularDamping	= body[kAngularDamping].get<float>();
			out.linearVelocity	= ParseVec3(body, kLinearVelocity);
			out.angularVelocity = ParseVec3(body, kAngularVelocity);
			return out;
		}

		static void ParseMovementConfig(const json& mv, OUT MovementConfig& cfg)
		{
			//cfg.walkSpeed		 = mv.value(kWalkSpeed, cfg.walkSpeed);
			//cfg.sprintSpeed		 = mv.value(kSprintSpeed, cfg.sprintSpeed);
			//cfg.crouchSpeed		 = mv.value(kCrouchSpeed, cfg.crouchSpeed);

			//cfg.accelGround		 = mv.value(kAccelGround, cfg.accelGround);
			//cfg.accelAir		 = mv.value(kAccelAir, cfg.accelAir);
			//cfg.frictionGround	 = mv.value(kFrictionGround, cfg.frictionGround);

			//cfg.gravity			 = mv.value(kGravity, cfg.gravity);
			//cfg.jumpSpeed		 = mv.value(kJumpSpeed, cfg.jumpSpeed);

			//cfg.coyoteTimeSec	 = mv.value(kCoyoteTimeSec, cfg.coyoteTimeSec);
			//cfg.jumpBufferSec	 = mv.value(kJumpBufferSec, cfg.jumpBufferSec);

			//cfg.standHeight		 = mv.value(kStandHeight, cfg.standHeight);
			//cfg.crouchHeight	 = mv.value(kCrouchHeight, cfg.crouchHeight);
			//cfg.slideHeight		 = mv.value(kSlideHeight, cfg.slideHeight);
			//cfg.radius			 = mv.value(kCharRadius, cfg.radius);

			//cfg.slideMinSpeed	 = mv.value(kSlideMinSpeed, cfg.slideMinSpeed);
			//cfg.slideDurationSec = mv.value(kSlideDurationSec, cfg.slideDurationSec);
			//cfg.slideFriction	 = mv.value(kSlideFriction, cfg.slideFriction);
			//cfg.slideSpeedCap	 = mv.value(kSlideSpeedCap, cfg.slideSpeedCap);
		}


		static PrefabCCTDef ParseCCTDef(const json& cj, INOUT PhysicsPrefabAsset& asset)
		{
			PrefabCCTDef out{};
			out.radius			= cj.value(kCctRadius, out.radius);
			out.height			= cj.value(kCctHeight, out.height);

			{
				const PrefabMaterialDef mat = ParseMaterialDef(cj.at(kMaterial));
				out.material = InternMaterial(asset, mat);
			}

			out.allowCrouch		= cj.value(kAllowCrouch, out.allowCrouch);
			out.allowSlide		= cj.value(kAllowSlide, out.allowSlide);

			out.contactOffset	= cj.value(kCctContactOffset, out.contactOffset);
			out.stepOffset		= cj.value(kStepOffset, out.stepOffset);
			out.slopeLimit		= cj.value(kSlopeLimit, out.slopeLimit);

			out.hasHitbox		= cj.value(kHasHitbox, out.hasHitbox);

			const json& mv = cj.value(kMovement, json::object());
			if (!mv.empty())
				ParseMovementConfig(mv, out.movement);

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

	PhysicsPrefabAsset PhysicsPrefabIO::LoadPrefabAssetFromFile(const std::string& path)
	{
		const json root = LoadPrefabJsonFromFile(path);
		return LoadPrefabAssetFromJson(root);
	}

	PhysicsPrefabAsset PhysicsPrefabIO::LoadPrefabAssetFromJson(const json& root)
	{
		PhysicsPrefabAsset asset{};
		asset.version = root[kVersion].get<int32>();
		if (asset.version != 1)
			throw std::runtime_error("Unsupported prefab version");

		for (const json& t : root[kTemplates])
		{
			PrefabTemplateDef out{};
			out.name			 = t[kName].get<string>();
			out.kind			 = ParseBodyKind(t);
			out.spawnPolicy		 = ParseSpawnPolicy(t);
			out.allowReplication = t[kAllowReplication].get<bool>();

			if (out.kind == ePrefabBodyKind::DYNAMIC)
			{
				out.dynamic = ParseDynamicBodyDef(t.at(kDynBody));
			}

			if (out.kind == ePrefabBodyKind::CHARACTER)
			{
				const json& cj = t.value(kCct, json::object());
				if (!cj.empty())
					out.cct = ParseCCTDef(cj, asset);

				if (out.cct.hasHitbox && !t.at(kShapes).empty())
					throw std::runtime_error("character template: cct.has_hitbox=true requires template.shapes to be empty");
			}

			for (const json& sj : t[kShapes])
			{
				PrefabShapeDef s = ParseShapeDef(sj, asset);
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

	PrefabLevelAsset PhysicsPrefabIO::LoadLevelAssetFromFile(const std::string& path)
	{
		const json root = LoadLevelJsonFromFile(path);
		return LoadLevelAssetFromJson(root);
	}

	PrefabLevelAsset PhysicsPrefabIO::LoadLevelAssetFromJson(const json& root)
	{
		PrefabLevelAsset out{};
		out.version = root.at(kVersion).get<int32>();
		if (out.version != 1)
			throw std::runtime_error("Unsupported level version");

		const json& insts = root.at("instances");
		if (!insts.is_array())
			throw std::runtime_error("level.instances must be array");

		out.instances.reserve(insts.size());

		for (const json& ij : insts)
		{
			PrefabLevelInstanceDef inst{};
			inst.templateName	= ij["template"].get<string>();
			inst.pose			= ParseTransform(ij, "pose");

			if (!ij.contains("overrides"))
			{
				out.instances.push_back(std::move(inst));
				continue;
			}

			const json& ov = ij.at("overrides");

			if (ov.contains(kLinearVelocity))
				inst.overrides.linearVelocity = ParseVec3(ov.at(kLinearVelocity));
			if (ov.contains(kAngularVelocity))
				inst.overrides.angularVelocity = ParseVec3(ov.at(kAngularVelocity));
			if (ov.contains(kLinearDamping))
				inst.overrides.linearDamping = ov[kLinearDamping].get<float>();
			if (ov.contains(kAngularDamping))
				inst.overrides.angularDamping = ov[kAngularDamping].get<float>();

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
