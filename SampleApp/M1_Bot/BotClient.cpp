#include "pch.h"
#include "BotClient.h"


namespace
{
	constexpr jam::net::AccountId kBotAccountBegin = 6000;
	constexpr jam::net::AccountId kBotAccountEnd = 9999;
}


BotClient::~BotClient()
{
	Shutdown();
}

bool BotClient::Init(jam::net::ClientConfig config)
{
	if (m_runtime || config.accountId < kBotAccountBegin || config.accountId > kBotAccountEnd)
		return false;

	const std::string credential = std::to_string(config.accountId);
	config.loginId = credential;
	config.password = credential;
	config.ticket.clear();
	config.headlessMode = true;

	m_accountId = config.accountId;
	m_runtime = std::make_unique<jam::net::ClientRuntime>(config);
	m_instanceId = m_runtime->GetInstanceId();
	m_state = eBotState::Disconnected;
	return true;
}

void BotClient::Shutdown()
{
	if (m_runtime)
		m_runtime->Shutdown();

	m_runtime.reset();
	m_userId = jam::net::kInvalidUserId;
	m_networkState = {};
	m_mainWorld = {};
	m_state = eBotState::Shutdown;
}

bool BotClient::Connect()
{
	if (!m_runtime)
		return false;

	if (!m_runtime->Connect())
		return false;

	m_state = eBotState::Connecting;
	return true;
}

void BotClient::Disconnect()
{
	if (!m_runtime)
		return;

	m_runtime->Disconnect();
	m_userId = jam::net::kInvalidUserId;
	m_networkState = {};
	m_mainWorld = {};
	m_state = eBotState::Disconnected;
}

jam::net::ClientPumpResult BotClient::Pump(const jam::net::ClientPumpOptions& options)
{
	return m_runtime ? m_runtime->Pump(options) : jam::net::ClientPumpResult{};
}

bool BotClient::PollEvent(jam::net::ClientEvent& outEvent)
{
	if (!m_runtime || !m_runtime->PollEvent(outEvent))
		return false;

	ApplyEvent(outEvent);
	return true;
}

jam::net::ClientRequestSubmission BotClient::RequestWorldAction(const jam::net::WorldActionCommand& command)
{
	return m_runtime ? m_runtime->RequestWorldAction(command) : jam::net::ClientRequestSubmission{};
}

jam::net::ClientRequestSubmission BotClient::RequestActorAction(const jam::net::ActorActionCommand& command)
{
	return m_runtime ? m_runtime->RequestActorAction(command) : jam::net::ClientRequestSubmission{};
}

jam::net::ClientRequestSubmission BotClient::RequestSocialCommand(const jam::net::SocialCommand& command)
{
	return m_runtime ? m_runtime->RequestSocialCommand(command) : jam::net::ClientRequestSubmission{};
}

jam::net::ClientRequestSubmission BotClient::RequestContent(const jam::net::GenericContentRequest& request)
{
	return m_runtime ? m_runtime->RequestContent(request) : jam::net::ClientRequestSubmission{};
}

void BotClient::SubmitCharacterControl(const jam::net::CharacterControlIntent& intent)
{
	if (m_runtime)
		m_runtime->SubmitCharacterControl(intent);
}

void BotClient::ApplyEvent(const jam::net::ClientEvent& event)
{
	switch (event.type)
	{
	case jam::net::eClientEventType::NetworkStateChanged:
	{
		const auto& network = std::get<jam::net::NetworkStateEvent>(event.payload);
		m_accountId = network.accountId;
		m_userId = network.userId;
		m_networkState = network.state;

		switch (network.state.phase)
		{
		case jam::net::eNetworkPhase::Connecting:
			m_state = eBotState::Connecting;
			break;
		case jam::net::eNetworkPhase::Ready:
			m_state = eBotState::Ready;
			break;
		case jam::net::eNetworkPhase::Degraded:
			m_state = eBotState::Degraded;
			break;
		case jam::net::eNetworkPhase::Disconnected:
			m_state = eBotState::Disconnected;
			m_userId = jam::net::kInvalidUserId;
			m_mainWorld = {};
			break;
		}
		break;
	}
	case jam::net::eClientEventType::WorldParticipantChanged:
		m_mainWorld = m_runtime->GetMainWorldRef();
		break;
	default:
		break;
	}
}
