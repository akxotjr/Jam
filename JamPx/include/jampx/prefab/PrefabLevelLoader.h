#pragma once
#include <string>
#include <vector>

namespace physx
{
	class PxRigidActor;
}

namespace jam::px::prefab
{

	class PrefabLevelLoader
	{
	public:

		void							Load(const std::string& layer, const std::string& levelPath);
		void							Unload(const std::string& layer);
		void							UnloadAll();

		void							SetPhysicsWorld(PhysicsWorld* world);

	private:
		void							LoadImpl(const std::string& layer, const std::string& levelPath);

	private:
		PhysicsWorld*									m_physicsWorld = nullptr;

		std::unordered_map<std::string, std::string>					m_layerToPath;
		std::unordered_map<std::string, std::vector<PxRigidActor*>>	m_layerActors;
	};
}