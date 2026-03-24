#pragma once
#include <string>
#include <vector>

namespace physx
{
	class PxRigidActor;
}

namespace jam::px
{
	struct LevelLoadResult
	{
		ObjectId	id			= INVALID_OBJ_ID;
		RigidBody	body;
		eActorType	actorType	= eActorType::None;
		eBodyType	bodyType	= eBodyType::None;
		eMotionType motionType	= eMotionType::None;
	};

	class PrefabLevelLoader
	{
	public:
		void							SetPhysicsWorld(PhysicsWorld* world);
		LevelLayerInfo					SetLevelPath(const std::string& path);
		std::vector<LevelLoadResult>	Load(const std::string& layer, INOUT std::vector<LevelInstanceInfo>& instances);

		std::vector<ObjectId>			Unload(const std::string& layer);
		std::vector<ObjectId>			UnloadAll();



	private:
		std::string													m_levelPath;
		PhysicsLevelAsset											m_asset = {};

		PhysicsWorld*												m_physicsWorld = nullptr;
		std::unordered_map<std::string, std::vector<ObjectId>>		m_layerActors;
	};
}