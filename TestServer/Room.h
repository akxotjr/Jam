#pragma once
#include "ActorComponents.h"
#include "PhysicsWorld.h"


class Room
{
public:
	Room();
	~Room();

	void Init();
	void Update();


	void							ApplyInput();


	void							CreatePlayer();
	void							CreateMonster();


private:
	void							SyncFromPhysics();


	const StaticActorDesc&			SpawnStaticActor();
	const DynamicActorDesc&			SpawnDynamicActor();



private:
	uint32							m_roomId = 0;

	entt::registry					m_registry;

	Uptr<PhysicsWorld>				m_physicsWorld;
};

