#include "pch.h"
#include "jampx/prefab/PrefabLevelLoader.h"
#include "jampx/prefab/PhysicsPrefabIO.h"
#include "jampx/prefab/PhysicsPrefabRegistry.h"
#include "jampx/PhysicsWorld.h"
#include "jampx/actor/ActorFactory.h"


namespace jam::px
{
	LevelLayerInfo PrefabLevelLoader::SetLevelPath(const std::string& path)
	{
		if (path.empty()) return {};

		m_levelPath = path;
		m_asset = PhysicsPrefabIO::LoadLevelAssetFromFile(m_levelPath);

		LevelLayerInfo info{};
		for (const auto& layer : m_asset.layers)
		{
			uint32 count = static_cast<uint32>(layer.instances.size());
			info.countPerLayer[layer.name] = count;
			info.totalCount += count;
		}

		return info;
	}

	std::vector<LevelLoadResult> PrefabLevelLoader::Load(const std::string& layer, INOUT std::vector<LevelInstanceInfo>& instances)
	{
		if (m_levelPath.empty() || layer.empty() || !m_physicsWorld)
			throw std::runtime_error("PrefabLevelLoader::Load - invalid state");

		auto itLayer = std::ranges::find_if(m_asset.layers, [&](const PhysicsLevelLayerDef& l) { return l.name == layer; });
		if (itLayer == m_asset.layers.end())
			throw std::runtime_error("PrefabLevelLoader::Load - layer not found in level: " + layer);

		if (!itLayer->enabled)
			return {};

		if (instances.size() != itLayer->instances.size())
			return {};

		std::vector<LevelLoadResult> results;
		results.reserve(itLayer->instances.size());

		auto& loadedIds = m_layerActors[layer];
		loadedIds.reserve(loadedIds.size() + itLayer->instances.size());

		for (size_t i = 0; i < itLayer->instances.size(); ++i)
		{
			auto& dst = instances[i];
			const auto& src = itLayer->instances[i];

			if (dst.objectId == INVALID_OBJ_ID)
				return {};

			dst.levelActorId = src.levelActorId;
			dst.prefab		 = MakePrefabKey(src.templateName);
			if (!dst.prefab.IsValid()) return {};

			const TemplateHandle	tpl = PHYSICS_PREFAB_REGISTRY.FindHandleByName(src.templateName);
			const ActorTemplateDef* def = PHYSICS_PREFAB_REGISTRY.FindTemplateDef(tpl);
			if (!def) return {};

			if (def->bodyType != eBodyType::Rigid)
				return {}; 

			SpawnDesc desc{};
			desc.pose		= ToPx(src.pose);
			desc.spawnSrc	= eSpawnSource::Level;
			desc.overrides	= src.overrides;

			auto rigidBody = ActorFactory::CreateRigidBody(*m_physicsWorld, tpl, *def, desc, dst.objectId);
			if (!rigidBody.has_value())
				return {};

			dst.state = rigidBody->GetMainState(); // FIX: move 전에 상태 추출

			LevelLoadResult out{
				.id			= dst.objectId,
				.body		= std::move(*rigidBody),
				.actorType	= def->actorType,
				.bodyType	= def->bodyType,
				.motionType = def->motionType
			};

			results.push_back(std::move(out));
			loadedIds.push_back(dst.objectId);
		}

		return results;
	}


	std::vector<ObjectId> PrefabLevelLoader::Unload(const std::string& layer)
	{
		auto it = m_layerActors.find(layer);
		if (it == m_layerActors.end())
			return {};

		std::vector<ObjectId> ids = std::move(it->second);
		m_layerActors.erase(it);
		return ids;
	}

	std::vector<ObjectId> PrefabLevelLoader::UnloadAll()
	{
		std::vector<ObjectId> out;
		for (auto& val : m_layerActors | std::views::values)
		{
			auto& ids = val;
			out.insert(out.end(), ids.begin(), ids.end());
		}
		m_layerActors.clear();
		return out;
	}


	void PrefabLevelLoader::SetPhysicsWorld(PhysicsWorld* world)
	{
		m_physicsWorld = world;
	}

}
