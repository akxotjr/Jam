#pragma once

#include "jamnet/core/executor/ShardOwnedObject.h"
#include "jamnet/core/net/Session.h"

#include <memory>

namespace jam
{
	struct ShardLocal;
}

namespace jam::net
{
	struct SessionShardState
	{
		uint32							shardIndex = 0;
		ShardOwnedObjectTable<Session>	sessionsById;

		SessionId								AdoptSession(std::unique_ptr<Session>& owner);
		std::unique_ptr<Session>				ReleaseSession(SessionId sessionId, const Session* expected = nullptr);

		Session*								FindSession(SessionId sessionId);
		const Session*							FindSession(SessionId sessionId) const;
		ShardOwnedObjectRefSlot<Session>		FindSessionRef(SessionId sessionId);
		ShardOwnedObjectRefSlot<const Session>	FindSessionRef(SessionId sessionId) const;
	};

	SessionShardState& GetOrCreateSessionShardState(ShardLocal& L);
	SessionShardState& GetOrCreateSessionShardState();
}
