#pragma once

#include "jamnet/core/executor/ShardOwnedObject.h"
#include "jamnet/core/net/TcpSession.h"

#include <memory>
#include <unordered_map>

namespace jam
{
	struct ShardLocal;
}

namespace jam::net
{
	using PreboundSessionTable = std::unordered_map<EndpointHandle, std::unique_ptr<Session>>;

	struct SessionShardState
	{
		uint32							shardIndex = 0;
		PreboundSessionTable			preboundSessions;
		ShardOwnedObjectTable<Session>	sessionsById;

		bool									AttachPreboundSession(std::unique_ptr<Session> session);
		std::unique_ptr<Session>				DetachPreboundSession(const EndpointHandle& handle);
		Session*								FindPreboundSession(const EndpointHandle& handle);

		SessionId								AllocSessionId();
		void									FreeSessionId(SessionId sessionId);
		bool									PromotePreboundSession(SessionId sessionId, std::unique_ptr<Session>& owner);
		std::unique_ptr<Session>				ReleaseBoundSession(SessionId sessionId, Session* expected = nullptr);
		Session*								FindSession(SessionId sessionId);
		const Session*							FindSession(SessionId sessionId) const;
		ShardOwnedObjectRefSlot<Session>		FindSessionRef(SessionId sessionId);
		ShardOwnedObjectRefSlot<const Session>	FindSessionRef(SessionId sessionId) const;

		Session* FindSessionAny(SessionId sessionId, const EndpointHandle& handle);
	};

	PreboundSessionTable&	GetPreboundSessionTable(ShardLocal& L);
	SessionShardState&		GetOrCreateSessionShardState(ShardLocal& L);
	SessionShardState&		GetOrCreateSessionShardState();
}
