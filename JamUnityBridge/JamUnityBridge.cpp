#include "pch.h"
#include "JamUnityBridge.h"
#include "UnityClientCore.h"

#include <memory>

static std::unique_ptr<UnityClientCore> g_core;

JAM_eResult JU_Initialize(const JAM_ClientConfig* config)
{
	if (!config)
		return JAM_eResult::InvalidArgument;
	if (config->structSize != sizeof(JAM_ClientConfig) || config->abiVersion != JAM_ABI_VERSION)
		return JAM_eResult::VersionMismatch;
	if (g_core)
		return JAM_eResult::Ok;

	g_core = std::make_unique<UnityClientCore>();
	if (g_core->Initialize(*config))
		return JAM_eResult::Ok;

	g_core.reset();
	return JAM_eResult::InternalError;
}

void JU_Shutdown()
{
	if (!g_core)
		return;

	g_core->Shutdown();
	g_core.reset();
}

JAM_eResult JU_Connect()
{
	if (!g_core)
		return JAM_eResult::NotInitialized;
	return g_core->Connect() ? JAM_eResult::Ok : JAM_eResult::InternalError;
}

void JU_Disconnect()
{
	if (g_core)
		g_core->Disconnect();
}

JAM_eResult JU_Pump(const JAM_ClientPumpOptions* options, JAM_ClientPumpResult* outResult)
{
	if (!outResult)
		return JAM_eResult::InvalidArgument;
	if (!g_core)
		return JAM_eResult::NotInitialized;
	if (options && options->structSize != sizeof(JAM_ClientPumpOptions))
		return JAM_eResult::VersionMismatch;

	return g_core->Pump(options, *outResult);
}

uint32_t JU_GetAbiVersion()
{
	return JAM_ABI_VERSION;
}

JAM_eResult JU_GetNetworkState(JAM_NetworkState* outState)
{
	return !outState ? JAM_eResult::InvalidArgument : !g_core ? JAM_eResult::NotInitialized : g_core->GetNetworkState(*outState);
}

JAM_eResult JU_GetAccountId(uint64_t* outAccountId)
{
	return !outAccountId ? JAM_eResult::InvalidArgument : !g_core ? JAM_eResult::NotInitialized : g_core->GetAccountId(*outAccountId);
}

JAM_eResult JU_GetUserId(uint64_t* outUserId)
{
	return !outUserId ? JAM_eResult::InvalidArgument : !g_core ? JAM_eResult::NotInitialized : g_core->GetUserId(*outUserId);
}

JAM_eResult JU_GetMainWorldRef(JAM_WorldRuntimeRef* outWorldRef)
{
	return !outWorldRef ? JAM_eResult::InvalidArgument : !g_core ? JAM_eResult::NotInitialized : g_core->GetMainWorldRef(*outWorldRef);
}

JAM_eResult JU_GetActorPresentationFramePair(uint64_t worldId, JAM_ActorState* outPreviousActors, int32_t previousCapacity, JAM_ActorFrame* outPreviousFrame, JAM_ActorState* outCurrentActors, int32_t currentCapacity, JAM_ActorFrame* outCurrentFrame, JAM_FrameCopyInfo* outInfo)
{
	if (!outPreviousFrame || !outCurrentFrame || !outInfo)
		return JAM_eResult::InvalidArgument;
	if (!g_core)
		return JAM_eResult::NotInitialized;

	return g_core->GetActorPresentationFramePair(worldId, outPreviousActors, previousCapacity, outPreviousFrame, outCurrentActors, currentCapacity, outCurrentFrame, *outInfo);
}

JAM_eResult JU_RequestWorldAction(const JAM_WorldActionCommand* command, JAM_ClientRequestSubmission* outSubmission)
{
	if (!command || !outSubmission)
		return JAM_eResult::InvalidArgument;
	if (!g_core)
		return JAM_eResult::NotInitialized;

	return g_core->RequestWorldAction(*command, *outSubmission);
}

JAM_eResult JU_RequestActorAction(const JAM_ActorActionCommand* command, JAM_ClientRequestSubmission* outSubmission)
{
	if (!command || !outSubmission)
		return JAM_eResult::InvalidArgument;
	if (!g_core)
		return JAM_eResult::NotInitialized;

	return g_core->RequestActorAction(*command, *outSubmission);
}

JAM_eResult JU_RequestSocialCommand(const JAM_SocialCommand* command, JAM_ClientRequestSubmission* outSubmission)
{
	if (!command || !outSubmission)
		return JAM_eResult::InvalidArgument;
	if (!g_core)
		return JAM_eResult::NotInitialized;

	return g_core->RequestSocialCommand(*command, *outSubmission);
}

JAM_eResult JU_SubmitCharacterControl(const JAM_CharacterControlIntent* intent)
{
	if (!intent)
		return JAM_eResult::InvalidArgument;
	if (!g_core)
		return JAM_eResult::NotInitialized;

	return g_core->SubmitCharacterControl(*intent);
}

JAM_eResult JU_PollEvent(JAM_ClientEvent* outEvent)
{
	return !outEvent ? JAM_eResult::InvalidArgument : !g_core ? JAM_eResult::NotInitialized : g_core->PollEvent(*outEvent);
}
