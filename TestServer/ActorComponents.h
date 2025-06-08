#pragma once
#include "pch.h"

enum class EActorType
{
	Player,
	Monster,
	Bot,
};


struct Position				{ float px, py, pz; };
struct Velocity				{ float vx, vy, vz; };
struct Rotation				{ float yaw, pitch; };

struct Speed				{ float hs, vs; };	// horizon speed, vertical speed

struct PhysicsBody			{ physx::PxActor* body; };

struct ActorType			{ EActorType type; };
struct ActorId				{ uint32 actorId; };


struct StaticActorDesc
{
	ActorId				id;
	ActorType			type;
	Position			position;
	Rotation			rotation;
	PhysicsBody			body;
};

struct DynamicActorDesc
{
	ActorId				id;
	ActorType			type;
	Position			position;
	Rotation			rotation;
	PhysicsBody			body;
	Velocity			velocity;
	Speed				speed;
};


