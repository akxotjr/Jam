#include "pch.h"
#include "JamUnityBridge.h"
#include "UnityClientCore.h"

#include <memory>

static std::unique_ptr<UnityClientCore> g_core;

bool JU_Initialize(const JUClientConfig* config)
{
	if (config == nullptr)
		return false;

	if (g_core)
		return true;

	g_core = std::make_unique<UnityClientCore>();
	if (g_core->Initialize(*config))
		return true;

	g_core.reset();
	return false;
}

void JU_Shutdown()
{
	if (!g_core)
		return;

	g_core->Shutdown();
	g_core.reset();
}

void JU_Pump(float deltaTime)
{
	if (g_core)
		g_core->Pump(deltaTime);
}

bool JU_IsConnected()
{
	return g_core && g_core->IsConnected();
}

JUNetworkPhase JU_GetNetworkPhase()
{
	return g_core ? g_core->GetNetworkPhase() : JUNetworkPhase::Disconnected;
}

uint64_t JU_GetUserId()
{
	return g_core ? g_core->GetUserId() : 0;
}

uint64_t JU_GetMainLocalWorldId()
{
	return g_core ? g_core->GetMainLocalWorldId() : 0;
}

uint64_t JU_GetMainWorldArchetypeKey()
{
	return g_core ? g_core->GetMainWorldArchetypeKey() : 0;
}

void JU_SubmitInput(const JUInputCommand* command)
{
	if (g_core && command)
		g_core->SubmitInput(*command);
}

void JU_RequestClickMove(const JUClickMoveCommand* command)
{
	if (g_core && command)
		g_core->RequestClickMove(*command);
}

int32_t JU_CopyActorFrame(JUActorState* outActors, int32_t actorCapacity, JUActorFrame* outFrame)
{
	return g_core ? g_core->CopyActorFrame(outActors, actorCapacity, outFrame) : 0;
}

JUEventType JU_PopEventType()
{
	if (!g_core)
		return JUEventType::None;

	return g_core->GetEventQueue().PopType();
}

bool JU_ReadNetworkStateEvent(JUNetworkStateEvent* outEvent)
{
	if (!g_core || !outEvent)
		return false;

	return g_core->GetEventQueue().ReadNetworkState(*outEvent);
}

bool JU_ReadWorldMembershipEvent(JUWorldMembershipEvent* outEvent)
{
	if (!g_core || !outEvent)
		return false;

	return g_core->GetEventQueue().ReadWorldMembership(*outEvent);
}

bool JU_ReadActorSpawnedEvent(JUActorSpawnedEvent* outEvent)
{
	if (!g_core || !outEvent)
		return false;

	return g_core->GetEventQueue().ReadActorSpawned(*outEvent);
}

bool JU_ReadActorDespawnedEvent(JUActorDespawnedEvent* outEvent)
{
	if (!g_core || !outEvent)
		return false;

	return g_core->GetEventQueue().ReadActorDespawned(*outEvent);
}

bool JU_ReadClickMoveResolvedEvent(JUClickMoveResolvedEvent* outEvent)
{
	if (!g_core || !outEvent)
		return false;

	return g_core->GetEventQueue().ReadClickMoveResolved(*outEvent);
}
