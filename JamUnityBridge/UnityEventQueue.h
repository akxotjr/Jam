#pragma once

#include <mutex>
#include <optional>
#include <queue>
#include <variant>

#include "JamUnityBridge.h"

struct JUQueuedEvent
{
	JUEventType type = JUEventType::None;
	std::variant<
		std::monostate,
		JUNetworkStateEvent,
		JUWorldMembershipEvent,
		JUActorSpawnedEvent,
		JUActorDespawnedEvent,
		JUClickMoveResolvedEvent> payload;
};

class UnityEventQueue
{
public:
	void Push(const JUNetworkStateEvent& e);
	void Push(const JUWorldMembershipEvent& e);
	void Push(const JUActorSpawnedEvent& e);
	void Push(const JUActorDespawnedEvent& e);
	void Push(const JUClickMoveResolvedEvent& e);

	JUEventType PopType();

	bool ReadNetworkState(JUNetworkStateEvent& outEvent);
	bool ReadWorldMembership(JUWorldMembershipEvent& outEvent);
	bool ReadActorSpawned(JUActorSpawnedEvent& outEvent);
	bool ReadActorDespawned(JUActorDespawnedEvent& outEvent);
	bool ReadClickMoveResolved(JUClickMoveResolvedEvent& outEvent);

	void Clear();

private:
	template <typename T>
	bool ReadCurrent(JUEventType expectedType, T& outEvent);

private:
	std::mutex m_mutex;
	std::queue<JUQueuedEvent> m_queue;
	std::optional<JUQueuedEvent> m_current;
};
