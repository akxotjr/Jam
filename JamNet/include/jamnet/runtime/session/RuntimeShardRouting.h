#pragma once

#include "jamnet/core/executor/Job.h"
#include "jamnet/core/net/Buffer.h"
#include "jamnet/runtime/session/UserContext.h"

#include <functional>

namespace jam::net
{
	enum class eProtocolType : uint8;
	class WorldBase;

	using WorldJob = std::function<void(WorldBase&)>;

	bool SubmitToSessionShard(SessionId sessionId, Job job);
	bool SubmitToUserShard(UserId userId, Job job);
	bool SubmitToWorldShard(WorldId worldId, Job job);

	bool SubmitWorldJob(const WorldRef& world, WorldJob job, eJobPriority priority = eJobPriority::Normal);

	void SendToUser(UserId userId, Packet packet, eProtocolType protocol);
	void MulticastToWorld(const WorldRef& world, Packet packet);
	void BroadcastToUsers(Packet packet);
}
