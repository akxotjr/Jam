#pragma once

#include <memory>
#include <string>
#include <vector>

#include "JamUnityBridge.h"
#include "jamnet/core/net/NetRuntime.h"
#include "jamnet/runtime/application/ClientRuntime.h"

class UnityClientCore
{
public:
	bool			Initialize(const JAM_ClientConfig& config);
	void			Shutdown();
	bool			Connect();
	void			Disconnect();
	JAM_eResult		Pump(const JAM_ClientPumpOptions* options, JAM_ClientPumpResult& outResult);

	JAM_eResult		GetNetworkState(JAM_NetworkState& outState) const;
	JAM_eResult		GetAccountId(uint64_t& outAccountId) const;
	JAM_eResult		GetUserId(uint64_t& outUserId) const;
	JAM_eResult		GetMainWorldRef(JAM_WorldRef& outWorldRef) const;
	JAM_eResult		GetActorPresentationFramePair(uint64_t worldId, JAM_ActorState* outPreviousActors, int32_t previousCapacity, JAM_ActorFrame* outPreviousFrame, JAM_ActorState* outCurrentActors, int32_t currentCapacity, JAM_ActorFrame* outCurrentFrame, JAM_FrameCopyInfo& outInfo) const;
	
	JAM_eResult		RequestWorldAction(const JAM_WorldActionCommand& command, JAM_ClientRequestSubmission& outSubmission);
	JAM_eResult		RequestActorAction(const JAM_ActorActionCommand& command, JAM_ClientRequestSubmission& outSubmission);
	JAM_eResult		RequestSocialCommand(const JAM_SocialCommand& command, JAM_ClientRequestSubmission& outSubmission);
	JAM_eResult		RequestGenericContent(const JAM_GenericContentRequest& request, JAM_ClientRequestSubmission& outSubmission);
	
	JAM_eResult		SubmitCharacterControl(const JAM_CharacterControlIntent& intent);
	
	JAM_eResult		PollEvent(JAM_ClientEvent& outEvent);

private:
	static int32_t	CopyFrameView(const jam::net::ActorPresentationFrameView& frame, JAM_ActorState* outActors, int32_t actorCapacity, JAM_ActorFrame* outFrame);

private:
	std::unique_ptr<jam::net::NetRuntime>		m_netRuntime;
	std::unique_ptr<jam::net::ClientRuntime>	m_runtime;
	std::vector<uint8_t>						m_eventPayload;
	std::string									m_eventRecipientName;
};
