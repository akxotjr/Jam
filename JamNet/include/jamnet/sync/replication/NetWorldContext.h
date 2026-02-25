#pragma once
#include "ReplicationTypes.h"


namespace jam::net
{
	// ---- common ---- 

	struct TickCounter
	{
		uint32 tick = 0;

		void Init() { tick = 0; }
		void Tick() { ++tick; }
	};

	// ---- client ----

	struct PendingServerStateQueue
	{
		deque<ServerState> states;
	};



	// ---- server ----







	// ---- helpers ----

	static void InitClientNetWorldCtx(entt::registry& world)
	{
		world.ctx().emplace<TickCounter>();
		world.ctx().emplace<PendingServerStateQueue>();
	}

	static void InitServerNetWorldCtx(entt::registry& world)
	{
		
	}
}
