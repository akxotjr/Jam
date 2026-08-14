#include "pch.h"
#include "BotScenario.h"

#include "BotClient.h"
#include "../M1_Shared/BotScenarioPlacement.h"

#include <Protocol/Cpp/character_content_generated.h>
#include <Protocol/Cpp/chat_content_generated.h>

#include <cmath>
#include <array>
#include <limits>

namespace
{
	constexpr jam::net::GenericContentOperationCode kCharacterListOperation = 1;
	constexpr jam::net::GenericContentOperationCode kCharacterSelectOperation = 2;
	constexpr uint16 kTextChatContentType = 1;
	constexpr auto kMovementHeartbeatInterval = std::chrono::milliseconds(100);

	std::vector<std::byte> CopyPayload(const flatbuffers::FlatBufferBuilder& builder)
	{
		const auto* begin = reinterpret_cast<const std::byte*>(builder.GetBufferPointer());
		return { begin, begin + builder.GetSize() };
	}

	std::vector<std::byte> MakeChatPayload(uint64 accountId, const char* channel, uint32 textLength)
	{
		std::string text = std::format("Bot{}-{}-", accountId, channel);
		text.resize(textLength, 'x');

		flatbuffers::FlatBufferBuilder builder(textLength + 64);
		const auto root = m1::fb::CreatefbChatText(builder, builder.CreateString(text), {});
		builder.Finish(root);
		return CopyPayload(builder);
	}
}


bool BotScenario::Configure(const BotScenarioConfig& config)
{
	if (m_state != eBotScenarioState::Idle || !config.IsValid())
		return false;
	m_config = config;
	return true;
}

void BotScenario::Start()
{
	Reset();
	m_state = eBotScenarioState::WaitingForReady;
}

void BotScenario::Reset()
{
	m_state = eBotScenarioState::Idle;
	m_failure = eBotScenarioFailure::None;
	m_pendingRequestId = jam::net::kInvalidClientRequestId;
	m_characterId = 0;
	m_lastAdmission = jam::net::eClientRequestAdmission::Accepted;
	m_lastContentStatus = jam::net::eGenericContentResponseStatus::None;
	m_chatPayload.clear();
	m_nextChat = {};
	m_chatSent = 0;
	m_chatRejected = 0;
	m_chatReceived = 0;
	m_chatStarted = false;
	m_movementState = eBotMovementState::Disabled;
	m_random.seed(m_config.movement.randomSeed);
	m_movementDeadline = {};
	m_nextMovementHeartbeat = {};
	m_activeMovementIntent = {};
	m_hasActiveMovementIntent = false;
	m_worldStayDeadline = {};
	m_currentWorld = {};
	m_previousWorld = {};
	m_expectedWorld = {};
	m_portalTarget = jam::px::Vec3::Zero();
	m_traverseAnchor = jam::px::Vec3::Zero();
	m_pairDirection = jam::px::Vec3::Zero();
	m_pairDistance = 0.0f;
	m_pairMoveDurationMs = 0;
	m_traverseLegStarted = false;
	m_traverseLaneIndex = 0;
	m_traverseInitialReverse = false;
	m_hotspotIndex = 0;
	m_squareStage = 0;
	m_squareMovesRemaining = 0;
	m_squareMoveDurationMs = 0;
	m_synchronizedPortalStarted = false;
	m_portalTransitions = 0;
	m_portalTimeouts = 0;
	m_consecutivePortalTimeouts = 0;
	m_unexpectedWorldTransitions = 0;
}

void BotScenario::Update(BotClient& client)
{
	if (m_state == eBotScenarioState::WaitingForReady && client.IsReady())
	{
		RequestCharacterList(client);
		return;
	}

	if (m_state == eBotScenarioState::RequestingEnterWorld && client.IsInWorld())
	{
		m_pendingRequestId = jam::net::kInvalidClientRequestId;
		m_state = eBotScenarioState::Running;

		if (m_config.chat.channel != eBotChatChannel::Direct)
			StartChat(client);

		StartMovement(client);
	}

	if (m_state == eBotScenarioState::Running && m_chatStarted)
		UpdateChat(client);
	if (m_state == eBotScenarioState::Running && m_config.movement.enabled)
		UpdateMovement(client);
}

void BotScenario::EnableDirectChat(BotClient& client)
{
	if (m_state == eBotScenarioState::Running && m_config.chat.channel == eBotChatChannel::Direct && !m_chatStarted)
		StartChat(client);
}

void BotScenario::BeginSynchronizedPortal(BotClient& client)
{
	if (m_state != eBotScenarioState::Running || m_config.movement.portalMode != eBotPortalMode::Synchronized || m_synchronizedPortalStarted)
		return;

	m_synchronizedPortalStarted = true;
	BeginPortalApproach(client);
}

void BotScenario::OnEvent(BotClient& client, const jam::net::ClientEvent& event)
{
	if (m_state == eBotScenarioState::Idle || m_state == eBotScenarioState::Failed)
		return;

	switch (event.type)
	{
	case jam::net::eClientEventType::NetworkStateChanged:
	{
		const auto& network = std::get<jam::net::NetworkStateEvent>(event.payload);
		if (network.state.phase == jam::net::eNetworkPhase::Disconnected)
		{
			Fail(eBotScenarioFailure::Disconnected);
		}
		break;
	}
	case jam::net::eClientEventType::ContentRequestCompleted:
		HandleContentResponse(client, std::get<jam::net::GenericContentResponseEvent>(event.payload).response);
		break;
	case jam::net::eClientEventType::WorldParticipantChanged:
		Update(client);
		break;
	case jam::net::eClientEventType::SocialMessageReceived:
		++m_chatReceived;
		break;
	default:
		break;
	}
}

void BotScenario::StartChat(BotClient& client)
{
	const auto now = std::chrono::steady_clock::now();
	if (m_config.chat.IsEnabled())
	{
		const char* channel = m_config.chat.channel == eBotChatChannel::Global ? "Global" : (m_config.chat.channel == eBotChatChannel::World ? "World" : "Direct");

		m_chatPayload	= MakeChatPayload(client.GetAccountId(), channel, m_config.chat.textLength);
		m_nextChat		= now + std::chrono::milliseconds(m_config.chat.intervalMs);
		m_chatStarted	= true;
	}
}

void BotScenario::UpdateChat(BotClient& client)
{
	const auto now = std::chrono::steady_clock::now();
	if (m_config.chat.IsEnabled() && now >= m_nextChat)
	{
		const auto audience = m_config.chat.channel == eBotChatChannel::Global ? jam::net::eSocialAudience::Global
			: (m_config.chat.channel == eBotChatChannel::World ? jam::net::eSocialAudience::Group : jam::net::eSocialAudience::Direct);

		SendChat(client, audience, m_chatPayload);
		m_nextChat = now + std::chrono::milliseconds(m_config.chat.intervalMs);
	}
}

void BotScenario::SendChat(BotClient& client, jam::net::eSocialAudience audience, const std::vector<std::byte>& payload)
{
	const uint64 scopeId = audience == jam::net::eSocialAudience::Group ? client.GetMainWorld().worldId : 0;
	jam::net::SocialRecipient recipient{};

	if (audience == jam::net::eSocialAudience::Direct)
	{
		recipient.kind = jam::net::eSocialRecipientKind::CharacterName;
		recipient.name = m_config.chat.directTargetName;
	}
	
	const auto submission = client.RequestSocialCommand({
		.destination = {
			.audience  = audience,
			.scopeId   = scopeId,
			.recipient = std::move(recipient),
		},
		.contentType = kTextChatContentType,
		.payload	 = payload,
	});

	if (!submission.Accepted())
	{
		++m_chatRejected;
		return;
	}

	++m_chatSent;
}

void BotScenario::StartMovement(BotClient& client)
{
	if (!m_config.movement.enabled || !client.IsInWorld())
		return;

	EnterWorldMovement(client, client.GetMainWorld().instance);
}

void BotScenario::UpdateMovement(BotClient& client)
{
	if (!client.IsInWorld())
		return;

	const auto world = client.GetMainWorld().instance;
	if (world != m_currentWorld)
	{
		if (m_currentWorld.IsValid())
		{
			if (m_movementState == eBotMovementState::ApproachingPortal && world == m_expectedWorld)
				++m_portalTransitions;
			else
				++m_unexpectedWorldTransitions;
		}
	
		EnterWorldMovement(client, world);
		return;
	}

	if (m_movementState == eBotMovementState::Disabled)
		return;

	const auto now = std::chrono::steady_clock::now();
	RefreshMovementIntent(client, now);
	if (m_config.movement.portalMode == eBotPortalMode::Distributed && m_movementState != eBotMovementState::ApproachingPortal && now >= m_worldStayDeadline)
	{
		BeginPortalApproach(client);
		return;
	}

	if (now < m_movementDeadline)
		return;

	switch (m_movementState)
	{
	case eBotMovementState::MovingOut:
		BeginIdle(client, true);
		break;
	case eBotMovementState::IdleBeforeReturn:
		BeginDirectional(client, true);
		break;
	case eBotMovementState::MovingBack:
		BeginIdle(client, false);
		break;
	case eBotMovementState::IdleBetweenPairs:
		BeginDirectional(client, false);
		break;
	case eBotMovementState::ApproachingPortal:
		++m_portalTimeouts;
		++m_consecutivePortalTimeouts;
		if (m_consecutivePortalTimeouts >= m_config.movement.maxPortalTimeouts)
		{
			m_expectedWorld = {};
			m_portalTarget = jam::px::Vec3::Zero();
			BeginAuthoredPattern(client);
			break;
		}
		SetMovementIntent(client, { .locomotion = jam::net::MoveToPositionIntent{ .target = m_portalTarget } });
		m_movementDeadline = now + std::chrono::milliseconds(m_config.movement.portalTimeoutMs);
		break;
	case eBotMovementState::ApproachingScenarioStart:
		BeginAuthoredPattern(client);
		break;
	case eBotMovementState::SquareMoving:
		BeginIdle(client, false);
		m_movementState = eBotMovementState::SquareIdle;
		break;
	case eBotMovementState::SquareIdle:
		BeginSquareMove(client);
		break;
	default:
		break;
	}
}

void BotScenario::EnterWorldMovement(BotClient& client, const jam::net::WorldInstanceRef& world)
{
	const auto oldWorld = m_currentWorld;
	if (oldWorld.IsValid())
		m_previousWorld = oldWorld;

	m_currentWorld				= world;
	m_expectedWorld				= {};
	m_portalTarget				= jam::px::Vec3::Zero();
	m_consecutivePortalTimeouts = 0;
	m_worldStayDeadline			= std::chrono::steady_clock::time_point::max();

	switch (m_config.movement.pattern)
	{
	case eBotMovementPattern::Traverse:
	{
		const auto placement = m1::shared::MakeBotTraversePlacement(client.GetAccountId(), static_cast<uint32>(m_config.movement.scenarioLayout->traverseLanes.size()));
		m_traverseLaneIndex = placement.laneIndex;

		const BotTraverseLane& lane = m_config.movement.scenarioLayout->traverseLanes[m_traverseLaneIndex];
		m_traverseInitialReverse = placement.reverse;

		const jam::px::Vec3 laneDirection(lane.direction.localX, 0.0f, lane.direction.localY);
		m_traverseAnchor	 = lane.start + laneDirection * (lane.length * placement.phase);
		m_traverseLegStarted = false;

		if (oldWorld.IsValid())
			SetMovementIntent(client, { .locomotion = jam::net::MoveToPositionIntent{ .target = m_traverseAnchor } });
		else
			SetMovementIntent(client, MakeStopIntent());

		m_movementState = eBotMovementState::ApproachingScenarioStart;

		m_movementDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(placement.startDelayMs);

		break;
	}
	case eBotMovementPattern::ClusterSquare:
	{
		m_hotspotIndex = m_config.movement.randomSeed % m_config.movement.scenarioLayout->hotspots.size();

		const BotHotspot& hotspot = m_config.movement.scenarioLayout->hotspots[m_hotspotIndex];

		std::uniform_real_distribution<float> xDistribution(-hotspot.halfExtentX, hotspot.halfExtentX);
		std::uniform_real_distribution<float> zDistribution(-hotspot.halfExtentZ, hotspot.halfExtentZ);

		const jam::px::Vec3 target = hotspot.center + jam::px::Vec3(xDistribution(m_random), 0.0f, zDistribution(m_random));

		SetMovementIntent(client, { .locomotion = jam::net::MoveToPositionIntent{ .target = target } });

		m_movementState = eBotMovementState::ApproachingScenarioStart;
		m_movementDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(m_config.movement.portalTimeoutMs);

		break;
	}
	case eBotMovementPattern::Idle:
		SetMovementIntent(client, MakeStopIntent());
		m_movementState = eBotMovementState::WaitingForSynchronizedPortal;
		break;
	}
}

void BotScenario::BeginAuthoredPattern(BotClient& client)
{
	m_worldStayDeadline = m_config.movement.portalMode == eBotPortalMode::Distributed ? std::chrono::steady_clock::now() + std::chrono::milliseconds(RandomDuration(m_config.movement.worldStayDuration)) : std::chrono::steady_clock::time_point::max();
	
	if (m_config.movement.pattern == eBotMovementPattern::Traverse)
	{
		BeginDirectional(client, false);
		return;
	}
	
	if (m_config.movement.pattern == eBotMovementPattern::ClusterSquare)
	{
		m_squareStage = static_cast<uint8>(m_config.movement.randomSeed % 4);
		m_squareMovesRemaining = 0;
		BeginSquareMove(client);
	}
}

void BotScenario::BeginDirectional(BotClient& client, bool returning)
{
	if (!returning)
	{
		const BotTraverseLane& lane = m_config.movement.scenarioLayout->traverseLanes[m_traverseLaneIndex];
		const jam::px::Vec3 perpendicularDirection(-lane.direction.localY, 0.0f, lane.direction.localX);

		m_pairDirection = m_traverseInitialReverse ? -perpendicularDirection : perpendicularDirection;

		const double scaledDuration = static_cast<double>(m_config.movement.moveDuration.fixedMs) * m_config.movement.movementScale;
		m_pairMoveDurationMs = static_cast<uint32>(std::clamp(scaledDuration, 1.0, static_cast<double>(std::numeric_limits<uint32>::max() / 2)));
		m_pairDistance = static_cast<float>(static_cast<double>(m_pairMoveDurationMs) * 5.0 / 1000.0);
	}

	const jam::px::Vec3 target = m_traverseAnchor + m_pairDirection * (returning ? -m_pairDistance : m_pairDistance);
	SetMovementIntent(client, { .locomotion = jam::net::MoveToPositionIntent{ .target = target } });
	
	m_movementState = returning ? eBotMovementState::MovingBack : eBotMovementState::MovingOut;
	const uint32 durationMs = m_traverseLegStarted ? m_pairMoveDurationMs * 2 : m_pairMoveDurationMs;
	m_traverseLegStarted = true;
	m_movementDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(durationMs);
}

void BotScenario::BeginIdle(BotClient& client, bool beforeReturn)
{
	SetMovementIntent(client, MakeStopIntent());

	m_movementState = beforeReturn ? eBotMovementState::IdleBeforeReturn : eBotMovementState::IdleBetweenPairs;
	m_movementDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(RandomDuration(m_config.movement.idleDuration));
}

void BotScenario::BeginPortalApproach(BotClient& client)
{
	const auto worldIt = m_config.movement.portalApproaches->find(m_currentWorld.instanceId);
	if (worldIt == m_config.movement.portalApproaches->end() || worldIt->second.empty())
	{
		SetMovementIntent(client, MakeStopIntent());
		m_movementState = eBotMovementState::Disabled;
		return;
	}

	const auto& portals = worldIt->second;
	const auto portalIt = std::ranges::find_if(portals, [this](const BotPortalApproach& portal)
		{
			return portal.destinationWorld != m_previousWorld;
		});

	const BotPortalApproach& portal = portalIt != portals.end() ? *portalIt : portals.front();
	
	m_expectedWorld = portal.destinationWorld;
	m_portalTarget = portal.position;
	m_consecutivePortalTimeouts = 0;
	
	SetMovementIntent(client, { .locomotion = jam::net::MoveToPositionIntent{ .target = m_portalTarget } });
	
	m_movementState = eBotMovementState::ApproachingPortal;
	m_movementDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(m_config.movement.portalTimeoutMs);
}

void BotScenario::BeginSquareMove(BotClient& client)
{
	static constexpr std::array<jam::net::DirectionalMoveIntent, 4> kDirections = {
		jam::net::DirectionalMoveIntent{ 0.0f, 1.0f },
		jam::net::DirectionalMoveIntent{ -1.0f, 0.0f },
		jam::net::DirectionalMoveIntent{ 0.0f, -1.0f },
		jam::net::DirectionalMoveIntent{ 1.0f, 0.0f },
	};

	if (m_squareMovesRemaining == 0)
	{
		const double scaledDuration = static_cast<double>(m_config.movement.moveDuration.fixedMs) * m_config.movement.movementScale;
		const BotHotspot& hotspot = m_config.movement.scenarioLayout->hotspots[m_hotspotIndex];
		const double hotspotDurationMs = static_cast<double>(std::min(hotspot.halfExtentX, hotspot.halfExtentZ)) / 5.0 * 1000.0;

		m_squareMoveDurationMs = static_cast<uint32>(std::clamp(std::min(scaledDuration, hotspotDurationMs), 1.0, static_cast<double>(std::numeric_limits<uint32>::max())));
		m_squareMovesRemaining = 4;
	}

	SetMovementIntent(client, { .locomotion = kDirections[m_squareStage] });
	
	m_squareStage = static_cast<uint8>((m_squareStage + 1) % kDirections.size());
	--m_squareMovesRemaining;
	m_movementState = eBotMovementState::SquareMoving;
	m_movementDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(m_squareMoveDurationMs);
}

void BotScenario::SetMovementIntent(BotClient& client, jam::net::CharacterControlIntent intent)
{
	m_activeMovementIntent = std::move(intent);
	m_hasActiveMovementIntent = true;
	client.SubmitCharacterControl(m_activeMovementIntent);
	m_nextMovementHeartbeat = std::chrono::steady_clock::now() + kMovementHeartbeatInterval;
}

void BotScenario::RefreshMovementIntent(BotClient& client, std::chrono::steady_clock::time_point now)
{
	if (!m_hasActiveMovementIntent || now < m_nextMovementHeartbeat)
		return;

	client.SubmitCharacterControl(m_activeMovementIntent);
	m_nextMovementHeartbeat = now + kMovementHeartbeatInterval;
}

uint32 BotScenario::RandomDuration(const BotDurationRange& range)
{
	if (range.randomMs == 0)
		return range.fixedMs;

	std::uniform_int_distribution<uint32> distribution(0, range.randomMs);
	const uint64 duration = static_cast<uint64>(range.fixedMs) + distribution(m_random);

	return static_cast<uint32>(std::min<uint64>(duration, std::numeric_limits<uint32>::max()));
}

jam::net::CharacterControlIntent BotScenario::MakeStopIntent() const
{
	return { .locomotion = jam::net::StopMovementIntent{} };
}

void BotScenario::RequestCharacterList(BotClient& client)
{
	const auto submission = client.RequestContent({ .opCode = kCharacterListOperation });
	m_lastAdmission = submission.admission;
	if (!submission.Accepted())
	{
		Fail(eBotScenarioFailure::RequestRejected);
		return;
	}

	m_pendingRequestId = submission.receipt.requestId;
	m_state = eBotScenarioState::RequestingCharacterList;
}

void BotScenario::RequestCharacterSelect(BotClient& client, uint64 characterId)
{
	flatbuffers::FlatBufferBuilder builder(64);
	const auto root = m1::fb::CreatefbCharacterSelectRequest(builder, characterId);
	builder.Finish(root);

	const auto submission = client.RequestContent({
		.opCode = kCharacterSelectOperation,
		.payload = CopyPayload(builder),
	});

	m_lastAdmission = submission.admission;
	if (!submission.Accepted())
	{
		Fail(eBotScenarioFailure::RequestRejected);
		return;
	}

	m_characterId		= characterId;
	m_pendingRequestId	= submission.receipt.requestId;
	m_state				= eBotScenarioState::RequestingCharacterSelect;
}

void BotScenario::RequestEnterWorld(BotClient& client)
{
	jam::net::EnterWorldRequest request{};
	if (!m_config.initialWorldName.empty())
	{
		request.archetypeKey	= m_config.initialWorldArchetype;
		request.selector		= jam::net::eWorldDestinationSelector::AuthoredDestination;
		request.destinationName = m_config.initialWorldName;
	}

	const auto submission = client.RequestWorldAction({
		.payload = std::move(request),
	});
	
	m_lastAdmission = submission.admission;
	if (!submission.Accepted())
	{
		Fail(eBotScenarioFailure::RequestRejected);
		return;
	}

	m_pendingRequestId = submission.receipt.requestId;
	m_state = eBotScenarioState::RequestingEnterWorld;
}

void BotScenario::HandleContentResponse(BotClient& client, const jam::net::GenericContentResponse& response)
{
	if (response.requestId != m_pendingRequestId)
		return;

	m_lastContentStatus = response.status;
	if (response.status != jam::net::eGenericContentResponseStatus::Succeeded)
	{
		Fail(eBotScenarioFailure::ContentRejected);
		return;
	}

	if (m_state == eBotScenarioState::RequestingCharacterList && response.opCode == kCharacterListOperation)
	{
		if (response.payload.empty())
		{
			Fail(eBotScenarioFailure::InvalidContentPayload);
			return;
		}

		flatbuffers::Verifier verifier(reinterpret_cast<const uint8*>(response.payload.data()), response.payload.size());
		if (!verifier.VerifyBuffer<m1::fb::fbCharacterListResponse>(nullptr))
		{
			Fail(eBotScenarioFailure::InvalidContentPayload);
			return;
		}

		const auto* result = flatbuffers::GetRoot<m1::fb::fbCharacterListResponse>(response.payload.data());
		const auto* characters = result->characters();
		if (!characters || characters->empty() || !characters->Get(0) || characters->Get(0)->character_id() == 0)
		{
			Fail(eBotScenarioFailure::CharacterNotFound);
			return;
		}

		RequestCharacterSelect(client, characters->Get(0)->character_id());
		return;
	}

	if (m_state == eBotScenarioState::RequestingCharacterSelect && response.opCode == kCharacterSelectOperation)
	{
		if (response.payload.empty())
		{
			Fail(eBotScenarioFailure::InvalidContentPayload);
			return;
		}

		flatbuffers::Verifier verifier(reinterpret_cast<const uint8*>(response.payload.data()), response.payload.size());
		if (!verifier.VerifyBuffer<m1::fb::fbCharacterSelectResponse>(nullptr))
		{
			Fail(eBotScenarioFailure::InvalidContentPayload);
			return;
		}

		const auto* result = flatbuffers::GetRoot<m1::fb::fbCharacterSelectResponse>(response.payload.data());
		if (!result->character() || result->character()->character_id() != m_characterId)
		{
			Fail(eBotScenarioFailure::InvalidContentPayload);
			return;
		}

		RequestEnterWorld(client);
	}
}

void BotScenario::Fail(eBotScenarioFailure failure)
{
	m_failure			= failure;
	m_pendingRequestId	= jam::net::kInvalidClientRequestId;
	m_state				= eBotScenarioState::Failed;
}
