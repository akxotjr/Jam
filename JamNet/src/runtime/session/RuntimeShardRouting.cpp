#include "pch.h"
#include "jamnet/runtime/session/RuntimeShardRouting.h"

#include "jamnet/core/executor/GlobalExecutor.h"
#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/core/net/Session.h"
#include "jamnet/core/net/SessionShardState.h"
#include "jamnet/runtime/world/lifecycle/WorldBase.h"
#include "jamnet/runtime/world/lifecycle/WorldShardState.h"


namespace jam::net
{
	bool SubmitToSessionShard(SessionId sessionId, Job job)
	{
		if (sessionId == kInvalidSessionId)
			return false;

		const uint16 shardIndex = GetRuntimeShardIndex(sessionId);
		if (const auto* local = CurrentShardLocal(); local && local->shardIndex == shardIndex)
		{
			job.Execute();
			return true;
		}

		const auto shard = GLOBAL_EXEC.GetShardFromIndex(shardIndex);
		if (!shard) return false;

		shard->Submit(std::move(job));
		return true;
	}

	bool SubmitToUserShard(UserId userId, Job job)
	{
		if (userId == kInvalidUserId)
			return false;

		const uint16 shardIndex = GetUserShardIndex(userId);
		if (const auto* local = CurrentShardLocal(); local && local->shardIndex == shardIndex)
		{
			job.Execute();
			return true;
		}

		const auto shard = GLOBAL_EXEC.GetShardFromIndex(shardIndex);
		if (!shard) return false;

		shard->Submit(std::move(job));
		return true;
	}

	bool SubmitToWorldShard(WorldId worldId, Job job)
	{
		if (worldId == kInvalidWorldId)
			return false;

		const uint16 shardIndex = GetWorldShardIndex(worldId);
		if (const auto* local = CurrentShardLocal(); local && local->shardIndex == shardIndex)
		{
			job.Execute();
			return true;
		}

		const auto shard = GLOBAL_EXEC.GetShardFromIndex(shardIndex);
		if (!shard) return false;

		shard->Submit(std::move(job));
		return true;
	}

	bool SubmitWorldJob(const WorldRef& worldRef, WorldJob job, eJobPriority priority)
	{
		if (!worldRef.IsValid() || !job)
			return false;

		return SubmitToWorldShard(worldRef.worldId, Job([worldRef, job = std::move(job)]() mutable
			{
				auto* world = GetOrCreateWorldShardState(CurrentShardLocalChecked()).FindWorld(worldRef.worldId);
				if (!world || world->GetWorldRef() != worldRef)
					return;

				job(*world);
			}, priority));
	}


	void SendToUser(UserId userId, Packet packet, eProtocolType protocol)
	{
		if (!packet.IsValid() || protocol == eProtocolType::NONE)
			return;

		SubmitToUserShard(userId, Job([userId, pkt = std::move(packet), protocol]() mutable
			{
				auto& local = CurrentShardLocalChecked();
				const UserContext* user = GetOrCreateUserShardState(local).FindUserContext(userId);
				if (!user)
					return;

				const SessionId sessionId = protocol == eProtocolType::TCP ? user->tcp : user->udp;
				if (sessionId == kInvalidSessionId)
					return;

				Session* session = GetOrCreateSessionShardState(local).FindSession(sessionId);
				if (!session || session->IsClosing() || !session->IsConnected())
					return;

				session->Send(std::move(pkt));
			}));
	}


	void MulticastToWorld(const WorldRef& world, Packet packet)
	{
		if (!packet.IsValid())
			return;

		SubmitWorldJob(world, [pkt = std::move(packet)](WorldBase& target) mutable
			{
				if (auto* host = dynamic_cast<WorldMembershipHost*>(&target))
					host->Multicast(std::move(pkt));
			});
	}

	void BroadcastToUsers(Packet packet)
	{
		if (!packet.IsValid())
			return;

		for (const auto& shard : GLOBAL_EXEC.GetShards())
		{
			if (!shard)
				continue;

			shard->Submit(Job([packet]() mutable
				{
					auto& local = CurrentShardLocalChecked();
					if (!local.usersState)
						return;

					for (const auto& entry : local.usersState->usersById.entries)
					{
						if (!entry.occupied || entry.value.userId == kInvalidUserId)
							continue;
						SendToUser(entry.value.userId, packet, eProtocolType::TCP);
					}
				}));
		}
	}
}
