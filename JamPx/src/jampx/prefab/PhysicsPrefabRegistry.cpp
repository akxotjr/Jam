#include "pch.h"
#include "jampx/prefab/PhysicsPrefabRegistry.h"
#include "jampx/prefab/PhysicsPrefabIO.h"
#include "jampx/prefab/PrefabAssetCreator.h"

namespace jam::px
{
	void PhysicsPrefabRegistry::Init(const std::string& prefabPath)
	{
		Clear();
		
		m_prefabPath = prefabPath;

		Load();
	}

	void PhysicsPrefabRegistry::Shutdown()
	{
		Clear();
	}

	void PhysicsPrefabRegistry::Clear()
	{
		// actor 먼저 정리 (shape ref 보유 가능)
		for (auto& r : m_rigidCache | std::views::values)
			if (r) r->release();
		m_rigidCache.clear();

		for (auto& s : m_shapeCache | std::views::values)
			if (s) s->release();
		m_shapeCache.clear();

		for (auto& t : m_triMeshCache | std::views::values)
			if (t) t->release();
		m_triMeshCache.clear();

		for (auto& c : m_cvxMeshCache | std::views::values)
			if (c) c->release();
		m_cvxMeshCache.clear();

		for (auto& m : m_materialCache | std::views::values)
			if (m) m->release();
		m_materialCache.clear();

		m_nameToHandle.clear();
		m_keyToHandle.clear();
		m_asset = {};
	}

	void PhysicsPrefabRegistry::Load()
	{
		Clear();

		m_asset = PhysicsPrefabIO::LoadPrefabAssetFromFile(m_prefabPath);

		m_nameToHandle.clear();
		m_keyToHandle.clear();

		// material stage
		for (const auto& [h, matDef] : m_asset.materials)
		{
			PxMaterial* mat = PrefabAssetCreator::CreateMaterial(matDef);
			if (!mat) throw std::runtime_error("CreateMaterial failed");
			m_materialCache.emplace(h, mat);
		}

		// mesh stage
		for (const auto& [h, meshDef] : m_asset.meshes)
		{
			switch (meshDef.type)
			{
			case eMeshType::Triangle:
			{
				PxTriangleMesh* tri = PrefabAssetCreator::CreateTriangleMesh(meshDef.cookedPath);
				if (!tri) throw std::runtime_error("CreateTriangleMesh failed: " + meshDef.cookedPath);
				m_triMeshCache.emplace(h, tri);
			}
			break;

			case eMeshType::Convex:
			{
				PxConvexMesh* cvx = PrefabAssetCreator::CreateConvexMesh(meshDef.cookedPath);
				if (!cvx) throw std::runtime_error("CreateConvexMesh failed: " + meshDef.cookedPath);
				m_cvxMeshCache.emplace(h, cvx);
			}
			break;
			}
		}

		// shape stage
		for (const auto& [h, shapeDef] : m_asset.shapes)
		{
			PxMaterial* mat = GetMaterial(shapeDef.material);
			PxShape* shape = nullptr;

			if (IsPrimitiveShape(shapeDef.type))
			{
				shape = PrefabAssetCreator::CreatePrimitiveShape(shapeDef, *mat);
			}
			else if (shapeDef.type == eShapeType::TriangleMesh)
			{
				shape = PrefabAssetCreator::CreateTriangleMeshShape(shapeDef, *mat, GetTriangleMesh(shapeDef.mesh));
			}
			else if (shapeDef.type == eShapeType::ConvexMesh)
			{
				shape = PrefabAssetCreator::CreateConvexMeshShape(shapeDef, *mat, GetConvexMesh(shapeDef.mesh));
			}
			else
			{
				throw std::runtime_error("unsupported shape type in registry load");
			}

			if (!shape)
				throw std::runtime_error("shape create failed");

			m_shapeCache.emplace(h, shape);
		}

		// rigid(template actor) stage
		for (const auto& [h, tmpDef] : m_asset.templates)
		{
			m_nameToHandle[tmpDef.name] = h;
			m_keyToHandle[fnv1a<uint64>(tmpDef.name)] = h;

			// Character/Non-rigid는 별도 경로
			if (!tmpDef.IsRigid()) continue;
			if (tmpDef.spawnPolicy != eSpawnPolicy::LevelOnly) continue;

			const auto& rigidDef = std::get<RigidBodyDef>(tmpDef.body);
			if (rigidDef.shapes.empty())
				throw std::runtime_error("rigid template has no shapes: " + tmpDef.name);

			std::vector<PxShape*> shapes;
			GetShapes(rigidDef.shapes, shapes);

			PxRigidActor* actor = PrefabAssetCreator::CreateRigidActor(tmpDef, shapes);
			if (!actor)
				throw std::runtime_error("CreateRigidActor failed: " + tmpDef.name);

			m_rigidCache.emplace(h, actor);
		}
	}

	bool PhysicsPrefabRegistry::HasTemplate(TemplateHandle h) const
	{
		return m_asset.templates.contains(h);
	}

	TemplateHandle PhysicsPrefabRegistry::FindHandleByName(const std::string& name) const
	{
		auto it = m_nameToHandle.find(name);
		return it != m_nameToHandle.end() ? it->second : TemplateHandle{};
	}

	TemplateHandle PhysicsPrefabRegistry::FindHandleByKey(PrefabKey key) const
	{
		if (!key.IsValid())
			return TemplateHandle{};

		auto it = m_keyToHandle.find(key.value);
		return it != m_keyToHandle.end() ? it->second : TemplateHandle{};
	}

	const ActorTemplateDef* PhysicsPrefabRegistry::FindTemplateDef(TemplateHandle h) const
	{
		auto it = m_asset.templates.find(h);
		return it != m_asset.templates.end() ? &it->second : nullptr;
	}

	const ActorTemplateDef* PhysicsPrefabRegistry::FindTemplateDef(PrefabKey key) const
	{
		auto h = FindHandleByKey(key);
		return FindTemplateDef(h);
	}

	eBodyType PhysicsPrefabRegistry::GetBodyType(const PrefabKey key)
	{
		if (!key.IsValid()) return eBodyType::None;
		
		auto h = FindHandleByKey(key);
		auto* def = FindTemplateDef(h);

		if (!def) return eBodyType::None;

		return def->bodyType;
	}

	eMotionType PhysicsPrefabRegistry::GetMotionType(const PrefabKey key)
	{
		if (!key.IsValid()) return eMotionType::None;

		auto h = FindHandleByKey(key);
		auto* def = FindTemplateDef(h);

		if (!def) return eMotionType::None;

		return def->motionType;
	}

	PxMaterial* PhysicsPrefabRegistry::GetMaterial(MaterialHandle h) const
	{
		auto it = m_materialCache.find(h);
		if (it == m_materialCache.end() || !it->second)
			throw std::runtime_error("material not resolved");
		return it->second;
	}

	PxTriangleMesh* PhysicsPrefabRegistry::GetTriangleMesh(MeshHandle h) const
	{
		auto it = m_triMeshCache.find(h);
		if (it == m_triMeshCache.end() || !it->second)
			throw std::runtime_error("triangle mesh not resolved");
		return it->second;
	}

	PxConvexMesh* PhysicsPrefabRegistry::GetConvexMesh(MeshHandle h) const
	{
		auto it = m_cvxMeshCache.find(h);
		if (it == m_cvxMeshCache.end() || !it->second)
			throw std::runtime_error("convex mesh not resolved");
		return it->second;
	}

	PxShape* PhysicsPrefabRegistry::GetShape(ShapeHandle h) const
	{
		auto it = m_shapeCache.find(h);
		if (it == m_shapeCache.end() || !it->second)
			throw std::runtime_error("shape not resolved");
		return it->second;
	}

	void PhysicsPrefabRegistry::GetShapes(const std::vector<ShapeHandle>& handles, OUT std::vector<PxShape*>& shapes) const
	{
		shapes.clear();
		shapes.reserve(handles.size());
		for (ShapeHandle h : handles)
			shapes.push_back(GetShape(h));
	}

	const ShapeDef& PhysicsPrefabRegistry::GetShapeDef(ShapeHandle h) const
	{
		auto it = m_asset.shapes.find(h);
		if (it == m_asset.shapes.end())
			throw std::runtime_error("shape def not resolved");
		return it->second;
	}

	const DynamicBodyDef& PhysicsPrefabRegistry::GetDynamicBodyDef(DynamicBodyHandle h) const
	{
		auto it = m_asset.dynBodies.find(h);
		if (it == m_asset.dynBodies.end())
			throw std::runtime_error("dynamic body def not resolved");
		return it->second;
	}

	const CCTBodyDef& PhysicsPrefabRegistry::GetCCTBodyDef(CCTBodyHandle h) const
	{
		auto it = m_asset.cctBodies.find(h);
		if (it == m_asset.cctBodies.end())
			throw std::runtime_error("cct body def not resolved");
		return it->second;
	}

	const CharacterMoveConfig& PhysicsPrefabRegistry::GetCharacterMoveConfig(CharacterMoveConfigHandle h) const
	{
		auto it = m_asset.charMoveConfigs.find(h);
		if (it == m_asset.charMoveConfigs.end())
			throw std::runtime_error("character move config not resolved");
		return it->second;
	}

	const KinematicDriverConfig& PhysicsPrefabRegistry::GetKinematicDriverConfig(KinematicDriverConfigHandle h) const
	{
		auto it = m_asset.kinematicDriverConfigs.find(h);
		if (it == m_asset.kinematicDriverConfigs.end())
			throw std::runtime_error("kinematic driver config not resolved");
		return it->second;
	}

	const ProjectileConfig& PhysicsPrefabRegistry::GetProjectileConfig(ProjectileConfigHandle h) const
	{
		auto it = m_asset.projectileConfigs.find(h);
		if (it == m_asset.projectileConfigs.end())
			throw std::runtime_error("projectile config not resolved");
		return it->second;
	}

	[[deprecated]]
	PxRigidActor* PhysicsPrefabRegistry::Instantiate(const PhysicsLevelInstanceDef& inst)
	{
		const TemplateHandle h = FindHandleByName(inst.templateName);
		if (!h || !HasTemplate(h))
			throw std::runtime_error("template not found. template= " + inst.templateName);

		const auto* def = FindTemplateDef(h);
		if (!def)
			throw std::runtime_error("template def not found. template= " + inst.templateName);

		if (def->spawnPolicy == eSpawnPolicy::RuntimeOnly)
			throw std::runtime_error("template is runtime-only. template= " + inst.templateName);

		const auto it = m_rigidCache.find(h);
		if (it == m_rigidCache.end() || !it->second)
			return nullptr;

		PxRigidActor* out	 = nullptr;
		PxRigidActor* cached = it->second;

		if (auto* sta = cached->is<PxRigidStatic>())
		{
			out = PxCloneStatic(*PX_PHYSICS, inst.pose, *sta);
		}
		else if (auto* dyn = cached->is<PxRigidDynamic>())
		{
			out = PxCloneDynamic(*PX_PHYSICS, inst.pose, *dyn);
		}

		if (!out) return nullptr;

		if (auto* dyn = out->is<PxRigidDynamic>())
		{
			const auto& ov = inst.overrides;
			if (ov.mask.has_any(SpawnOverrideMask::LINEAR_VEL))
				dyn->setLinearVelocity(ToPhysX(ov.linearVelocity));
			if (ov.mask.has_any(SpawnOverrideMask::ANGULAR_VEL))
				dyn->setAngularVelocity(ToPhysX(ov.angularVelocity));
			if (ov.mask.has_any(SpawnOverrideMask::LINEAR_DAMP))
				dyn->setLinearDamping(ov.linearDamping);
			if (ov.mask.has_any(SpawnOverrideMask::ANGULAR_DAMP))
				dyn->setAngularDamping(ov.angularDamping);
		}

		return out;
	}

	// runtime spawn path
	PxRigidActor* PhysicsPrefabRegistry::Instantiate(TemplateHandle tpl, const PxTransform& pose, void* userData)
	{
		const auto* def = FindTemplateDef(tpl);
		if (!def || !def->IsRigid()) return nullptr;

		const auto& rigidDef = std::get<RigidBodyDef>(def->body);

		std::vector<PxShape*> exclusiveShapes;
		exclusiveShapes.reserve(rigidDef.shapes.size());

		for (auto h : rigidDef.shapes)
		{
			const auto it = m_shapeCache.find(h);
			if (it == m_shapeCache.end()) return nullptr;

			const PxShape* cached = it->second;
			PxShape* cloned = physx::PxCloneShape(*PX_PHYSICS, *cached, true);

			if (!cloned)
			{
				for (PxShape* s : exclusiveShapes) if (s) s->release();
				return nullptr;
			}
			
			exclusiveShapes.push_back(cloned);
		}

		PxRigidActor* out = PrefabAssetCreator::CreateRigidActor(*def, exclusiveShapes);

		for (PxShape* s : exclusiveShapes)
			if (s) s->release();

		if (!out) return nullptr;

		out->setGlobalPose(pose);
		out->userData = userData;
		return out;
	}

	PxRigidActor* PhysicsPrefabRegistry::Instantiate(const std::string& name, const PxTransform& worldPose, void* userData)
	{
		const TemplateHandle h = FindHandleByName(name);
		return h ? Instantiate(h, worldPose, userData) : nullptr;
	}
}
