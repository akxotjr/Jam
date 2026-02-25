#include "pch.h"
#include "jampx/prefab/PrefabLevelLoader.h"
#include "jampx/prefab/PhysicsPrefabIO.h"
#include "jampx/prefab/PhysicsPrefabRegistry.h"
#include "jampx/PhysicsWorld.h"


namespace jam::px::prefab
{

	void PrefabLevelLoader::Load(const string& layer, const string& levelPath)
	{
		m_layerToPath[layer] = levelPath;
		LoadImpl(layer, levelPath);
	}

	void PrefabLevelLoader::Unload(const string& layer)
	{
		if (!m_physicsWorld) return;

		auto it = m_layerActors.find(layer);
		if (it == m_layerActors.end())
			return;

		for (auto* actor : it->second)
		{
			if (!actor) continue;
			m_physicsWorld->RemoveActor(actor);
		}

		it->second.clear();
		m_layerActors.erase(it);
		m_layerToPath.erase(layer);
	}

	void PrefabLevelLoader::UnloadAll()
	{
		if (!m_physicsWorld) return;

		for (auto& actors : m_layerActors | views::values)
		{
			for (auto* actor : actors)
			{
				if (!actor) continue;
				m_physicsWorld->RemoveActor(actor);
			}
		}

		m_layerActors.clear();
		m_layerToPath.clear();
	}

	void PrefabLevelLoader::SetPhysicsWorld(jam::px::PhysicsWorld* world)
	{
		m_physicsWorld = world;
	}

	void PrefabLevelLoader::LoadImpl(const std::string& layer, const std::string& levelPath)
	{
		if (!m_physicsWorld)
			throw std::runtime_error("PrefabLevelLoader::Load - physics world is null");

		PxScene* scene = m_physicsWorld->GetScene();
		if (!scene) throw std::runtime_error("PrefabLevelLoader::Load - physics scene is null");

		// 파일=레이어 정책: UnloadLayer/UnloadAll 전까지 append
		const PrefabLevelAsset level = PhysicsPrefabIO::LoadLevelAssetFromFile(levelPath);

		auto& spawned = m_layerActors[layer];
		spawned.reserve(spawned.size() + level.instances.size());

		for (const auto& inst : level.instances)
		{
			auto* actor = PHYSICS_PREFAB_REGISTRY.Instantiate(inst);
			if (!actor)
				throw std::runtime_error("Instantiate failed. template= " + inst.templateName);

			scene->addActor(*actor);
			spawned.push_back(actor);
		}
	}
}
