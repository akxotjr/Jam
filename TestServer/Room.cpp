#include "pch.h"
#include "Room.h"
#include "ActorComponents.h"
#include "PhysicsManager.h"

void Room::Init()
{
	m_physicsWorld = make_unique<PhysicsWorld>();
}

void Room::Update()
{
	m_physicsWorld->Simulation();
	SyncFromPhysics();
}

void Room::CreatePlayer()
{
	const DynamicActorDesc& desc = SpawnDynamicActor();

	auto entity = m_registry.create();
	m_registry.emplace<Position>(entity, desc.position);
	m_registry.emplace<Velocity>(entity, desc.velocity);
	m_registry.emplace<Rotation>(entity, desc.rotation);
	m_registry.emplace<Speed>(entity, desc.speed);
	m_registry.emplace<ActorId>(entity, desc.id);
	m_registry.emplace<ActorType>(entity, desc.type);
	m_registry.emplace<PxActor*>(entity, desc.body);

	m_physicsWorld->AddActor(desc.body.body);
}

void Room::SyncFromPhysics()
{
	auto view = m_registry.view<PhysicsBody, Position, Rotation>();
	for (auto [e, body, pos, rot] : view.each())
	{
		auto dynamicActor = reinterpret_cast<PxRigidDynamic*>(body.body);
		const PxTransform& t = dynamicActor->getGlobalPose();
		pos.px = t.p.x;
		pos.py = t.p.y;
		pos.pz = t.p.z;

		PxQuat q = t.q;
		//// TODO: yaw/pitch °è»ê
		//rot.yaw = ...;
		//rot.pitch = ...;
	}
}

const DynamicActorDesc& Room::SpawnDynamicActor()
{
	DynamicActorDesc desc;
	desc.position = Position(0, 0, 0);
	desc.velocity = Velocity(0, 0, 0);
	desc.rotation = Rotation(0, 0);
	desc.speed = Speed(5, 5);
	desc.type = ActorType(EActorType::Player);
	desc.id = ActorId(1);
	desc.body = PhysicsBody(PhysicsManager::Instance().CreateRigidDynamic(desc.position, desc.rotation));

	return desc;
}
