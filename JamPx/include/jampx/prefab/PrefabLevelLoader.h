#pragma once
#include <string>
#include <vector>

namespace physx
{
	class PxRigidActor;
}

namespace jam::px::prefab
{
	using namespace std;


	class PrefabLevelLoader
	{
	public:

		void							Load(const string& layer, const string& levelPath);
		void							Unload(const string& layer);
		void							UnloadAll();

		void							SetPhysicsWorld(PhysicsWorld* world);

	private:
		void							LoadImpl(const string& layer, const string& levelPath);

	private:
		PhysicsWorld*									m_physicsWorld = nullptr;

		unordered_map<string, string>					m_layerToPath;
		unordered_map<string, vector<PxRigidActor*>>	m_layerActors;
	};
}