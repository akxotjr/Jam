#include "pch.h"
#include "jampx/prefab/PhysicsArchetypeRegistry.h"

#include <ranges>

#include "jampx/PhysicsCore.h"
#include "jampx/prefab/PhysicsAssetLoader.h"
#include "jampx/prefab/PxCreator.h"

namespace jam::px
{
	void PhysicsArchetypeRegistry::Init(const std::string& assetPath, std::string_view assetName)
	{
		Clear();
		
		m_assetPath = assetPath;
		m_assetName = std::string(assetName);
		m_assetKey  = m_assetName.empty() ? PhysicsAssetKey{} : MakeAssetKey<PhysicsAssetTag>(m_assetName);

		Load();
	}

	void PhysicsArchetypeRegistry::Shutdown()
	{
		Clear();
	}

	void PhysicsArchetypeRegistry::Clear()
	{
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

		m_nameToKey.clear();
		m_assetName.clear();
		m_assetPath.clear();
		m_assetKey = {};
		m_db = {};
	}

	void PhysicsArchetypeRegistry::Load()
	{
		// Keep configured asset identity/path. Only runtime caches and loaded asset are rebuilt here.
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

		m_nameToKey.clear();
		m_db = {};

		m_db = PhysicsAssetLoader::Load(m_assetPath);
		if (!m_assetName.empty() && !IsValidAssetKey(m_assetKey))
			throw std::runtime_error("physics asset identity is invalid");

		m_nameToKey.clear();

		// material stage
		for (const auto& [h, matDef] : m_db.materials)
		{
			PxMaterial* mat = PxCreator::CreateMaterial(matDef);
			if (!mat) throw std::runtime_error("CreateMaterial failed");
			m_materialCache.emplace(h, mat);
		}

		// mesh stage
		for (const auto& [h, meshDef] : m_db.meshes)
		{
			switch (meshDef.type)
			{
			case eMeshType::Triangle:
			{
				PxTriangleMesh* tri = PxCreator::CreateTriangleMesh(meshDef.cookedPath);
				if (!tri) throw std::runtime_error("CreateTriangleMesh failed: " + meshDef.cookedPath);
				m_triMeshCache.emplace(h, tri);
			}
			break;

			case eMeshType::Convex:
			{
				PxConvexMesh* cvx = PxCreator::CreateConvexMesh(meshDef.cookedPath);
				if (!cvx) throw std::runtime_error("CreateConvexMesh failed: " + meshDef.cookedPath);
				m_cvxMeshCache.emplace(h, cvx);
			}
			break;
			}
		}

		// shape stage
		for (const auto& [h, shapeDef] : m_db.shapes)
		{
			PxMaterial* mat = GetMaterial(shapeDef.material);
			PxShape* shape = nullptr;

			if (IsPrimitiveShape(shapeDef.type))
			{
				shape = PxCreator::CreatePrimitiveShape(shapeDef, *mat);
			}
			else if (shapeDef.type == eShapeType::TriangleMesh)
			{
				shape = PxCreator::CreateTriangleMeshShape(shapeDef, *mat, GetTriangleMesh(shapeDef.mesh));
			}
			else if (shapeDef.type == eShapeType::ConvexMesh)
			{
				shape = PxCreator::CreateConvexMeshShape(shapeDef, *mat, GetConvexMesh(shapeDef.mesh));
			}
			else
			{
				throw std::runtime_error("unsupported shape type in registry load");
			}

			if (!shape)
				throw std::runtime_error("shape create failed");

			m_shapeCache.emplace(h, shape);
		}

		for (const auto& [key, archetypeDef] : m_db.archetypes)
			m_nameToKey[archetypeDef.name] = key;
	}

	bool PhysicsArchetypeRegistry::HasArchetype(PhysicsArchetypeKey key) const
	{
		return m_db.archetypes.contains(key);
	}

	PhysicsArchetypeKey PhysicsArchetypeRegistry::FindKeyByName(const std::string& name) const
	{
		auto it = m_nameToKey.find(name);
		return it != m_nameToKey.end() ? it->second : PhysicsArchetypeKey{};
	}

	const PhysicsArchetypeData* PhysicsArchetypeRegistry::FindArchetype(PhysicsArchetypeKey key) const
	{
		auto it = m_db.archetypes.find(key);
		return it != m_db.archetypes.end() ? &it->second : nullptr;
	}

	eBodyType PhysicsArchetypeRegistry::GetBodyType(const PhysicsArchetypeKey key) const
	{
		if (!IsValidAssetKey(key)) return eBodyType::None;
		
		auto* def = FindArchetype(key);

		if (!def) return eBodyType::None;

		return def->bodyType;
	}

	eMotionType PhysicsArchetypeRegistry::GetMotionType(const PhysicsArchetypeKey key) const
	{
		if (!IsValidAssetKey(key)) return eMotionType::None;

		auto* def = FindArchetype(key);

		if (!def) return eMotionType::None;

		return def->motionType;
	}

	PxMaterial* PhysicsArchetypeRegistry::GetMaterial(MaterialHandle h) const
	{
		auto it = m_materialCache.find(h);
		if (it == m_materialCache.end() || !it->second)
			throw std::runtime_error("material not resolved");
		return it->second;
	}

	PxTriangleMesh* PhysicsArchetypeRegistry::GetTriangleMesh(MeshHandle h) const
	{
		auto it = m_triMeshCache.find(h);
		if (it == m_triMeshCache.end() || !it->second)
			throw std::runtime_error("triangle mesh not resolved");
		return it->second;
	}

	PxConvexMesh* PhysicsArchetypeRegistry::GetConvexMesh(MeshHandle h) const
	{
		auto it = m_cvxMeshCache.find(h);
		if (it == m_cvxMeshCache.end() || !it->second)
			throw std::runtime_error("convex mesh not resolved");
		return it->second;
	}

	PxShape* PhysicsArchetypeRegistry::GetShape(ShapeHandle h) const
	{
		auto it = m_shapeCache.find(h);
		if (it == m_shapeCache.end() || !it->second)
			throw std::runtime_error("shape not resolved");
		return it->second;
	}

	void PhysicsArchetypeRegistry::GetShapes(const std::vector<ShapeHandle>& handles, OUT std::vector<PxShape*>& shapes) const
	{
		shapes.clear();
		shapes.reserve(handles.size());
		for (ShapeHandle h : handles)
			shapes.push_back(GetShape(h));
	}

	const ShapeData& PhysicsArchetypeRegistry::GetShapeDef(ShapeHandle h) const
	{
		auto it = m_db.shapes.find(h);
		if (it == m_db.shapes.end())
			throw std::runtime_error("shape def not resolved");
		return it->second;
	}

	const DynamicBodyData& PhysicsArchetypeRegistry::GetDynamicBodyDef(DynamicBodyHandle h) const
	{
		auto it = m_db.dynBodies.find(h);
		if (it == m_db.dynBodies.end())
			throw std::runtime_error("dynamic body def not resolved");
		return it->second;
	}

	const CCTBodyData& PhysicsArchetypeRegistry::GetCCTBodyDef(CCTBodyHandle h) const
	{
		auto it = m_db.cctBodies.find(h);
		if (it == m_db.cctBodies.end())
			throw std::runtime_error("cct body def not resolved");
		return it->second;
	}

	const CharacterMoveConfig& PhysicsArchetypeRegistry::GetCharacterMoveConfig(CharacterMoveConfigHandle h) const
	{
		auto it = m_db.charMoveConfigs.find(h);
		if (it == m_db.charMoveConfigs.end())
			throw std::runtime_error("character move config not resolved");
		return it->second;
	}

	const KinematicDriverConfig& PhysicsArchetypeRegistry::GetKinematicDriverConfig(KinematicDriverConfigHandle h) const
	{
		auto it = m_db.kinematicDriverConfigs.find(h);
		if (it == m_db.kinematicDriverConfigs.end())
			throw std::runtime_error("kinematic driver config not resolved");
		return it->second;
	}

	const ProjectileConfig& PhysicsArchetypeRegistry::GetProjectileConfig(ProjectileConfigHandle h) const
	{
		auto it = m_db.projectileConfigs.find(h);
		if (it == m_db.projectileConfigs.end())
			throw std::runtime_error("projectile config not resolved");
		return it->second;
	}

	// runtime spawn path
	PxRigidActor* PhysicsArchetypeRegistry::Instantiate(PhysicsArchetypeKey key, const PxTransform& pose, void* userData)
	{
		const auto* def = FindArchetype(key);
		if (!def || !def->IsRigid()) return nullptr;

		const auto& rigidDef = std::get<RigidBodyData>(def->body);

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

		const DynamicBodyData* dynDef = rigidDef.dynamic ? &GetDynamicBodyDef(rigidDef.dynamic) : nullptr;
		PxRigidActor* out = PxCreator::CreateRigidActor(*def, exclusiveShapes, dynDef);

		for (PxShape* s : exclusiveShapes)
			if (s) s->release();

		if (!out) return nullptr;

		out->setGlobalPose(pose);
		out->userData = userData;
		return out;
	}

	PxRigidActor* PhysicsArchetypeRegistry::Instantiate(const std::string& name, const PxTransform& worldPose, void* userData)
	{
		const PhysicsArchetypeKey key = FindKeyByName(name);
		return key ? Instantiate(key, worldPose, userData) : nullptr;
	}
}
