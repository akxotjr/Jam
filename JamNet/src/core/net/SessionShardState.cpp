#include "pch.h"
#include "jamnet/core/executor/RuntimeId.h"
#include "jamnet/core/executor/ShardExecutor.h"
#include "jamnet/core/net/SessionShardState.h"

#include "jamnet/core/executor/ThreadContext.h"


namespace jam::net
{
	bool SessionShardState::AttachPreboundSession(std::unique_ptr<Session> session)
	{
		if (!session) 
			return false;
		return preboundSessions.emplace(session->GetEndpointHandle(), std::move(session)).second;
	}

	std::unique_ptr<Session> SessionShardState::DetachPreboundSession(const EndpointHandle& handle)
	{
		if (auto it = preboundSessions.find(handle); it != preboundSessions.end())
		{
			std::unique_ptr<Session> session = std::move(it->second);
			preboundSessions.erase(it);
			return session;
		}
		return {};
	}

	Session* SessionShardState::FindPreboundSession(const EndpointHandle& handle)
	{
		if (!handle.IsValid())
			return nullptr;

		if (auto it = preboundSessions.find(handle); it != preboundSessions.end())
			return it->second.get();

		return nullptr;
	}

	SessionId SessionShardState::AllocSessionId()
	{
		return sessionsById.AllocId(shardIndex);
	}

	void SessionShardState::FreeSessionId(SessionId sessionId)
	{
		sessionsById.FreeId(sessionId);
	}

	bool SessionShardState::PromotePreboundSession(SessionId sessionId, std::unique_ptr<Session>& owner)
	{
		if (sessionId == kInvalidSessionId || !owner || owner->GetSessionId() != sessionId)
			return false;

		auto* entry = sessionsById.FindEntry(sessionId);
		if (!entry || entry->object || entry->state == eShardOwnedObjectState::Closing)
			return false;

		entry->object = std::move(owner);
		entry->state  = eShardOwnedObjectState::Alive;
		return true;
	}

	std::unique_ptr<Session> SessionShardState::ReleaseBoundSession(SessionId sessionId, Session* expected)
	{
		auto* entry = sessionsById.FindEntry(sessionId);
		if (!entry || !entry->object)
			return {};
		if (expected && entry->object.get() != expected)
			return {};

		return std::move(entry->object);
	}

	Session* SessionShardState::FindSession(SessionId sessionId)
	{
		return sessionsById.Find(sessionId);
	}

	const Session* SessionShardState::FindSession(SessionId sessionId) const
	{
		return sessionsById.Find(sessionId);
	}

	ShardOwnedObjectRefSlot<Session> SessionShardState::FindSessionRef(SessionId sessionId)
	{
		return sessionsById.FindRef(sessionId);
	}

	ShardOwnedObjectRefSlot<const Session> SessionShardState::FindSessionRef(SessionId sessionId) const
	{
		return sessionsById.FindRef(sessionId);
	}

	Session* SessionShardState::FindSessionAny(SessionId sessionId, const EndpointHandle& handle)
	{
		Session* session = FindSession(sessionId);
		if (session) return session;
		session = FindPreboundSession(handle);

		return session;
	}

	PreboundSessionTable& GetPreboundSessionTable(ShardLocal& L)
	{
		return GetOrCreateSessionShardState(L).preboundSessions;
	}

	SessionShardState& GetOrCreateSessionShardState(ShardLocal& L)
	{
		if (!L.sessionState)
		{
			L.sessionState = std::make_shared<SessionShardState>();
			L.sessionState->shardIndex = L.shardIndex;
		}
		return *L.sessionState;
	}

	SessionShardState& GetOrCreateSessionShardState()
	{
		auto& L = CurrentShardLocalChecked();
		return GetOrCreateSessionShardState(L);
	}
}
