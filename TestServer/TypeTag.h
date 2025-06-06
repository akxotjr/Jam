#pragma once


enum class EActorType
{
	Player,
	Monster,
	Bot,
};

struct ActorType { EActorType type; };
struct ActorId { uint32 actorId; };
