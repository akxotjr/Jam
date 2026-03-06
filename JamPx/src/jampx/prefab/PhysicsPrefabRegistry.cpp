#include "pch.h"
#include "jampx/prefab/PhysicsPrefabRegistry.h"
#include "jampx/prefab/PhysicsPrefabIO.h"
#include "jampx/prefab/PrefabAssetCreator.h"

namespace jam::px::prefab
{
	void PhysicsPrefabRegistry::Init(const string& prefabPath)
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
		for (auto& s : m_shapeCache | views::values)
			if (s) s->release();
		m_shapeCache.clear();

		for (auto& m : m_materialCache | views::values)
			if (m) m->release();
		m_materialCache.clear();

		for (auto& r : m_rigidCache | views::values)
			if (r) r->release();
		m_rigidCache.clear();

		m_nameToHandle.clear();
		m_asset = {};
	}

	void PhysicsPrefabRegistry::Load()
	{
		Clear();

		m_asset = PhysicsPrefabIO::LoadPrefabAssetFromFile(m_prefabPath);

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
			case eShapeType::TRIANGLE_MESH:
			{
				PxTriangleMesh* tri = PrefabAssetCreator::CreateTriangleMesh(meshDef.cookedPath);
				if (!tri) throw std::runtime_error("CreateTriangleMesh failed: " + meshDef.cookedPath);
				m_triMeshCache.emplace(h, tri);
			}
			break;

			case eShapeType::CONVEX_MESH:
			{
				PxConvexMesh* cvx = PrefabAssetCreator::CreateConvexMesh(meshDef.cookedPath);
				if (!cvx) throw std::runtime_error("CreateConvexMesh failed: " + meshDef.cookedPath);
				m_cvxMeshCache.emplace(h, cvx);
			}
			break;

			default:
				break;
			}
		}

		// shape stage
		for (const auto& [h, shapeDef] : m_asset.shapes)
		{
			PxMaterial* mat = GetMaterial(shapeDef.material);

			PxShape* shape = nullptr;

			if (IsPrimtiveShape(shapeDef.type))
			{
				shape = PrefabAssetCreator::CreatePrimitiveShape(shapeDef, *mat);
			}
			else if (shapeDef.type == eShapeType::TRIANGLE_MESH)
			{
				shape = PrefabAssetCreator::CreateTriangleMeshShape(shapeDef, *mat, GetTriangleMesh(shapeDef.mesh));
			}
			else if (shapeDef.type == eShapeType::CONVEX_MESH)
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

			const uint64 key64 = fnv1a<uint64>(tmpDef.name);
			m_keyToHandle[key64] = h;

			if (tmpDef.actorType == eActorType::Character)
				continue; // 별도 시스템에서 처리

			vector<PxShape*> shapes;
			GetShapes(tmpDef.shapes, shapes);

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

	TemplateHandle PhysicsPrefabRegistry::FindHandleByName(const string& name) const
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

	const PrefabTemplateDef* PhysicsPrefabRegistry::FindTemplateDef(TemplateHandle h) const
	{
		auto it = m_asset.templates.find(h);
		return it != m_asset.templates.end() ? &it->second : nullptr;
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

	void PhysicsPrefabRegistry::GetShapes(const vector<ShapeHandle>& handles, OUT vector<PxShape*>& shapes) const
	{
		shapes.clear();
		shapes.reserve(handles.size());
		for (ShapeHandle h : handles)
			shapes.push_back(GetShape(h));
	}

	PxRigidActor* PhysicsPrefabRegistry::Instantiate(const PrefabLevelInstanceDef& inst)
	{
		const TemplateHandle h = FindHandleByName(inst.templateName);
		if (!h || !HasTemplate(h))
			throw std::runtime_error("template not found. template= " + inst.templateName);

		const auto* def = FindTemplateDef(h);
		if (!def)
			throw std::runtime_error("template def not found. template= " + inst.templateName);

		if (def->spawnPolicy == ePrefabSpawnPolicy::RUNTIME_ONLY)
			throw std::runtime_error("template is runtime-only. template= " + inst.templateName);

		const auto it = m_rigidCache.find(h);
		if (it == m_rigidCache.end() || !it->second)
			return nullptr;

		PxRigidActor* out = nullptr;
		PxRigidActor* cached = it->second;

		const auto& overrides = inst.overrides;

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
			if (overrides.linearVelocity.has_value())  dyn->setLinearVelocity(overrides.linearVelocity.value());
			if (overrides.angularVelocity.has_value()) dyn->setAngularVelocity(overrides.angularVelocity.value());
			if (overrides.linearDamping.has_value())   dyn->setLinearDamping(overrides.linearDamping.value());
			if (overrides.angularDamping.has_value())  dyn->setAngularDamping(overrides.angularDamping.value());
		}

		return out;
	}


	PxRigidActor* PhysicsPrefabRegistry::Instantiate(TemplateHandle h, const PxTransform& worldPose, void* userData)
	{
		const auto it = m_rigidCache.find(h);
		if (it == m_rigidCache.end() || !it->second)
			return nullptr;

		PxRigidActor* out = nullptr;
		PxRigidActor* cached = it->second;

		if (auto* sta = cached->is<PxRigidStatic>())
		{
			out = PxCloneStatic(*PX_PHYSICS, worldPose, *sta);
		}
		else if (auto* dyn = cached->is<PxRigidDynamic>())
		{
			out = PxCloneDynamic(*PX_PHYSICS, worldPose, *dyn);
		}

		if (out)
			out->userData = userData;

		return out;
	}

	PxRigidActor* PhysicsPrefabRegistry::Instantiate(const string& name, const PxTransform& worldPose, void* userData)
	{
		const TemplateHandle h = FindHandleByName(name);
		return h ? Instantiate(h, worldPose, userData) : nullptr;
	}
}
