#pragma once

#include <jamnet/runtime/application/ClientRuntime.h>

#include <chrono>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>


class BotClient;

enum class eBotChatChannel : uint8
{
	Disabled,
	Global,
	World,
	Direct,
};

enum class eBotMovementPattern : uint8
{
	Idle,
	Traverse,
	ClusterSquare,
};

enum class eBotPortalMode : uint8
{
	Disabled,
	Distributed,
	Synchronized,
};

struct BotChatConfig
{
	eBotChatChannel channel		= eBotChatChannel::Disabled;
	uint32			intervalMs	= 1000;
	uint32			textLength	= 64;
	std::string		directTargetName;

	bool IsEnabled() const { return channel != eBotChatChannel::Disabled; }
	bool IsValid() const
	{
		return channel <= eBotChatChannel::Direct
			&& (!IsEnabled() || (intervalMs > 0 && textLength > 0 && textLength <= 512))
			&& (channel != eBotChatChannel::Direct || !directTargetName.empty());
	}
};

struct BotDurationRange
{
	uint32 fixedMs  = 0;
	uint32 randomMs = 0;
};

struct BotPortalApproach
{
	jam::net::WorldInstanceRef	destinationWorld{};
	jam::px::Vec3				position = jam::px::Vec3::Zero();
};

using BotPortalApproaches = std::unordered_map<jam::net::WorldInstanceId, std::vector<BotPortalApproach>, jam::net::WorldInstanceIdHash>;

struct BotTraverseLane
{
	jam::px::Vec3					start = jam::px::Vec3::Zero();
	jam::net::DirectionalMoveIntent direction{};
	float							length = 0.0f;
};

struct BotHotspot
{
	jam::px::Vec3	center		= jam::px::Vec3::Zero();
	float			halfExtentX = 0.0f;
	float			halfExtentZ = 0.0f;
};

struct BotScenarioLayout
{
	std::vector<BotTraverseLane>	traverseLanes;
	std::vector<BotHotspot>			hotspots;
};

struct BotMovementConfig
{
	bool				enabled				= false;
	eBotMovementPattern pattern				= eBotMovementPattern::Traverse;
	eBotPortalMode		portalMode			= eBotPortalMode::Distributed;
	BotDurationRange	moveDuration		= { .fixedMs = 1000 };
	BotDurationRange	idleDuration		= { .fixedMs = 1000, .randomMs = 1000 };
	BotDurationRange	worldStayDuration	= { .fixedMs = 20000, .randomMs = 5000 };
	float				movementScale		= 1.0f;
	uint32				portalTimeoutMs		= 45000;
	uint32				maxPortalTimeouts	= 3;
	uint32				randomSeed			= 0;

	std::shared_ptr<const BotPortalApproaches>  portalApproaches;
	std::shared_ptr<const BotScenarioLayout>	scenarioLayout;

	bool IsValid() const
	{
		return !enabled || (movementScale > 0.0f
			&& (pattern == eBotMovementPattern::Idle || (scenarioLayout && !scenarioLayout->traverseLanes.empty() && !scenarioLayout->hotspots.empty()))
			&& (portalMode != eBotPortalMode::Distributed || worldStayDuration.fixedMs + static_cast<uint64>(worldStayDuration.randomMs) > 0)
			&& portalTimeoutMs > 0
			&& maxPortalTimeouts > 0
			&& (portalMode == eBotPortalMode::Disabled || (portalApproaches && !portalApproaches->empty())));
	}
};

struct BotScenarioConfig
{
	BotChatConfig				chat;
	BotMovementConfig			movement;
	std::string					initialWorldName;
	jam::net::WorldArchetypeKey initialWorldArchetype{};

	bool IsValid() const
	{
		return chat.IsValid() && movement.IsValid() && (initialWorldName.empty() == !jam::IsValidAssetKey(initialWorldArchetype));
	}
};

enum class eBotMovementState
{
	Disabled,
	MovingOut,
	IdleBeforeReturn,
	MovingBack,
	IdleBetweenPairs,
	ApproachingPortal,
	ApproachingScenarioStart,
	SquareMoving,
	SquareIdle,
	WaitingForSynchronizedPortal,
	Count,
};

enum class eBotScenarioState
{
	Idle,
	WaitingForReady,
	RequestingCharacterList,
	RequestingCharacterSelect,
	RequestingEnterWorld,
	Running,
	Failed,
};

enum class eBotScenarioFailure
{
	None,
	Disconnected,
	RequestRejected,
	ContentRejected,
	InvalidContentPayload,
	CharacterNotFound,
};


class BotScenario
{
public:
	bool									Configure(const BotScenarioConfig& config);
	void									Start();
	void									Reset();
	void									Update(BotClient& client);
	void									OnEvent(BotClient& client, const jam::net::ClientEvent& event);
	void									EnableDirectChat(BotClient& client);
	void									BeginSynchronizedPortal(BotClient& client);

	eBotScenarioState						GetState() const { return m_state; }
	eBotScenarioFailure						GetFailure() const { return m_failure; }
	bool									IsRunning() const { return m_state == eBotScenarioState::Running; }
	bool									HasFailed() const { return m_state == eBotScenarioState::Failed; }

	jam::net::ClientRequestId				GetPendingRequestId() const { return m_pendingRequestId; }
	uint64									GetCharacterId() const { return m_characterId; }
	jam::net::eClientRequestAdmission		GetLastAdmission() const { return m_lastAdmission; }
	jam::net::eGenericContentResponseStatus GetLastContentStatus() const { return m_lastContentStatus; }

	uint64									GetChatSent() const { return m_chatSent; }
	uint64									GetChatRejected() const { return m_chatRejected; }
	uint64									GetChatReceived() const { return m_chatReceived; }
	uint64									GetPortalTransitions() const { return m_portalTransitions; }
	uint64									GetPortalTimeouts() const { return m_portalTimeouts; }
	uint64									GetUnexpectedWorldTransitions() const { return m_unexpectedWorldTransitions; }

private:
	void									RequestCharacterList(BotClient& client);
	void									RequestCharacterSelect(BotClient& client, uint64 characterId);
	void									RequestEnterWorld(BotClient& client);
	void									HandleContentResponse(BotClient& client, const jam::net::GenericContentResponse& response);
	void									StartChat(BotClient& client);
	void									UpdateChat(BotClient& client);
	void									SendChat(BotClient& client, jam::net::eSocialAudience audience, const std::vector<std::byte>& payload);
	void									StartMovement(BotClient& client);
	void									UpdateMovement(BotClient& client);
	void									EnterWorldMovement(BotClient& client, const jam::net::WorldInstanceRef& world);
	void									BeginAuthoredPattern(BotClient& client);
	void									BeginDirectional(BotClient& client, bool returning);
	void									BeginIdle(BotClient& client, bool beforeReturn);
	void									BeginPortalApproach(BotClient& client);
	void									BeginSquareMove(BotClient& client);
	void									SetMovementIntent(BotClient& client, jam::net::CharacterControlIntent intent);
	void									RefreshMovementIntent(BotClient& client, std::chrono::steady_clock::time_point now);
	uint32									RandomDuration(const BotDurationRange& range);

	jam::net::CharacterControlIntent		MakeStopIntent() const;
	
	void									Fail(eBotScenarioFailure failure);

private:
	eBotScenarioState						m_state = eBotScenarioState::Idle;
	eBotScenarioFailure						m_failure = eBotScenarioFailure::None;
	jam::net::ClientRequestId				m_pendingRequestId = jam::net::kInvalidClientRequestId;
	uint64									m_characterId = 0;
	jam::net::eClientRequestAdmission		m_lastAdmission = jam::net::eClientRequestAdmission::Accepted;
	jam::net::eGenericContentResponseStatus m_lastContentStatus = jam::net::eGenericContentResponseStatus::None;

	BotScenarioConfig						m_config = {};
	std::vector<std::byte>					m_chatPayload;
	std::chrono::steady_clock::time_point	m_nextChat = {};
	uint64									m_chatSent = 0;
	uint64									m_chatRejected = 0;
	uint64									m_chatReceived = 0;
	bool									m_chatStarted = false;

	eBotMovementState						m_movementState = eBotMovementState::Disabled;
	std::mt19937							m_random;
	std::chrono::steady_clock::time_point	m_movementDeadline = {};
	std::chrono::steady_clock::time_point	m_nextMovementHeartbeat = {};
	jam::net::CharacterControlIntent		m_activeMovementIntent{};
	bool									m_hasActiveMovementIntent = false;
	std::chrono::steady_clock::time_point	m_worldStayDeadline = {};
	jam::net::WorldInstanceRef				m_currentWorld{};
	jam::net::WorldInstanceRef				m_previousWorld{};
	jam::net::WorldInstanceRef				m_expectedWorld{};
	jam::px::Vec3							m_portalTarget = jam::px::Vec3::Zero();
	jam::px::Vec3							m_traverseAnchor = jam::px::Vec3::Zero();
	jam::px::Vec3							m_pairDirection = jam::px::Vec3::Zero();
	float									m_pairDistance = 0.0f;
	uint32									m_pairMoveDurationMs = 0;
	bool									m_traverseLegStarted = false;
	size_t									m_traverseLaneIndex = 0;
	bool									m_traverseInitialReverse = false;
	size_t									m_hotspotIndex = 0;
	uint8									m_squareStage = 0;
	uint8									m_squareMovesRemaining = 0;
	uint32									m_squareMoveDurationMs = 0;
	bool									m_synchronizedPortalStarted = false;
	uint64									m_portalTransitions = 0;
	uint64									m_portalTimeouts = 0;
	uint32									m_consecutivePortalTimeouts = 0;
	uint64									m_unexpectedWorldTransitions = 0;
};
