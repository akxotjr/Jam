#pragma once

#include <memory>

#include <jamnet/runtime/application/ClientRuntime.h>

enum class eBotState
{
	Uninitialized,
	Disconnected,
	Connecting,
	Ready,
	Degraded,
	Shutdown,
};


class BotClient
{
public:
	BotClient() = default;
	~BotClient();

	bool								Init(jam::net::ClientConfig config);
	void								Shutdown();
	bool								Connect();
	void								Disconnect();

	jam::net::ClientPumpResult			Pump(const jam::net::ClientPumpOptions& options = {});
	bool								PollEvent(jam::net::ClientEvent& outEvent);

	jam::net::ClientRequestSubmission	RequestWorldAction(const jam::net::WorldActionCommand& command);
	jam::net::ClientRequestSubmission	RequestActorAction(const jam::net::ActorActionCommand& command);
	jam::net::ClientRequestSubmission	RequestSocialCommand(const jam::net::SocialCommand& command);
	jam::net::ClientRequestSubmission	RequestContent(const jam::net::GenericContentRequest& request);

	void								SubmitCharacterControl(const jam::net::CharacterControlIntent& intent);

	bool								IsInitialized()		const { return m_runtime != nullptr; }
	bool								IsReady()			const { return m_state == eBotState::Ready; }
	bool								IsInWorld()			const { return m_mainWorld.IsValid(); }

	uint64								GetInstanceId()		const { return m_instanceId; }
	jam::net::AccountId					GetAccountId()		const { return m_accountId; }
	jam::net::UserId					GetUserId()			const { return m_userId; }
	eBotState							GetState()			const { return m_state; }
	const jam::net::NetworkState&		GetNetworkState()	const { return m_networkState; }
	const jam::net::WorldRef&			GetMainWorld()		const { return m_mainWorld; }

private:
	void ApplyEvent(const jam::net::ClientEvent& event);

private:
	std::unique_ptr<jam::net::ClientRuntime> m_runtime;

	uint64					m_instanceId	= 0;
	jam::net::AccountId		m_accountId		= jam::net::kInvalidAccountId;
	jam::net::UserId		m_userId		= jam::net::kInvalidUserId;
	jam::net::NetworkState	m_networkState	= {};
	jam::net::WorldRef		m_mainWorld		= {};
	eBotState				m_state			= eBotState::Uninitialized;
};
