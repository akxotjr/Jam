#include "pch.h"
#include "UnityEventQueue.h"

void UnityEventQueue::Push(const JUNetworkStateEvent& e)
{
	std::lock_guard lock(m_mutex);
	m_queue.push(JUQueuedEvent{ JUEventType::NetworkState, e });
}

void UnityEventQueue::Push(const JUWorldMembershipEvent& e)
{
	std::lock_guard lock(m_mutex);
	m_queue.push(JUQueuedEvent{ JUEventType::WorldMembership, e });
}

void UnityEventQueue::Push(const JUActorSpawnedEvent& e)
{
	std::lock_guard lock(m_mutex);
	m_queue.push(JUQueuedEvent{ JUEventType::ActorSpawned, e });
}

void UnityEventQueue::Push(const JUActorDespawnedEvent& e)
{
	std::lock_guard lock(m_mutex);
	m_queue.push(JUQueuedEvent{ JUEventType::ActorDespawned, e });
}

void UnityEventQueue::Push(const JUClickMoveResolvedEvent& e)
{
	std::lock_guard lock(m_mutex);
	m_queue.push(JUQueuedEvent{ JUEventType::ClickMoveResolved, e });
}

JUEventType UnityEventQueue::PopType()
{
	std::lock_guard lock(m_mutex);

	if (m_queue.empty())
	{
		m_current.reset();
		return JUEventType::None;
	}

	m_current = std::move(m_queue.front());
	m_queue.pop();
	return m_current->type;
}

bool UnityEventQueue::ReadNetworkState(JUNetworkStateEvent& outEvent)
{
	return ReadCurrent(JUEventType::NetworkState, outEvent);
}

bool UnityEventQueue::ReadWorldMembership(JUWorldMembershipEvent& outEvent)
{
	return ReadCurrent(JUEventType::WorldMembership, outEvent);
}

bool UnityEventQueue::ReadActorSpawned(JUActorSpawnedEvent& outEvent)
{
	return ReadCurrent(JUEventType::ActorSpawned, outEvent);
}

bool UnityEventQueue::ReadActorDespawned(JUActorDespawnedEvent& outEvent)
{
	return ReadCurrent(JUEventType::ActorDespawned, outEvent);
}

bool UnityEventQueue::ReadClickMoveResolved(JUClickMoveResolvedEvent& outEvent)
{
	return ReadCurrent(JUEventType::ClickMoveResolved, outEvent);
}

void UnityEventQueue::Clear()
{
	std::lock_guard lock(m_mutex);
	m_current.reset();
	m_queue = {};
}

template <typename T>
bool UnityEventQueue::ReadCurrent(JUEventType expectedType, T& outEvent)
{
	std::lock_guard lock(m_mutex);
	if (!m_current.has_value() || m_current->type != expectedType)
		return false;

	outEvent = std::get<T>(m_current->payload);
	m_current.reset();
	return true;
}
