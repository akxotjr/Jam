#include "pch.h"
#include "jamnet/core/net/SessionShardState.h"

#include "jamnet/core/executor/ShardExecutor.h"
#include "jamnet/core/executor/ThreadContext.h"


namespace jam::net
{
	SessionId SessionShardState::AdoptSession(std::unique_ptr<Session>& owner)
	{
		if (!owner)
			return kInvalidSessionId;

		const SessionId sessionId = sessionsById.AllocId(shardIndex);
		if (sessionId == kInvalidSessionId)
			return kInvalidSessionId;

		auto* entry = sessionsById.FindEntry(sessionId);
		if (!entry || entry->object || entry->state == eShardOwnedObjectState::Closing)
		{
			sessionsById.FreeId(sessionId);
			return kInvalidSessionId;
		}

		entry->object = std::move(owner);
		entry->state  = eShardOwnedObjectState::Alive;
		return sessionId;
	}

	std::unique_ptr<Session> SessionShardState::ReleaseSession(SessionId sessionId, const Session* expected)
	{
		auto* entry = sessionsById.FindEntry(sessionId);
		if (!entry || !entry->object)
			return {};
		if (expected && entry->object.get() != expected)
			return {};

		std::unique_ptr<Session> owner = std::move(entry->object);
		sessionsById.FreeId(sessionId);
		return owner;
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
