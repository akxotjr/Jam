#include "pch.h"
#include "jamnet/runtime/world/lifecycle/ServerWorldSyncSystem.h"
#include "jamnet/runtime/application/ServerNetworkManager.h"
#include "jamnet/runtime/world/lifecycle/ServerWorldTransitionCoordinator.h"
#include "jamnet/runtime/world/lifecycle/WorldShardState.h"
#include "jamnet/runtime/world/simulation/server/ServerWorld.h"
#include "jamnet/runtime/session/RuntimeShardRouting.h"
#include "jamnet/runtime/session/ServerSession.h"
#include "jamnet/runtime/protocol/codec/WorldCodec.h"
#include "jamnet/runtime/protocol/transport/CustomPacketHelper.h"
#include "jamnet/core/utils/Clock.h"
#include "jamnet/core/executor/ThreadContext.h"

namespace jam::net
{
	UserContext* ServerWorldSyncSystem::FindUserOnCurrentShard(uint64 userId) const
	{
		auto* local = CurrentShardLocal();
		if (!local || local->shardIndex != GetUserShardIndex(userId))
			return nullptr;
		return GetOrCreateUserShardState(*local).FindUserContext(userId);
	}

	WorldSyncToken ServerWorldSyncSystem::IssueWorldSyncToken()
	{
		for (;;)
		{
			const uint64 value = m_nextWorldSyncToken.fetch_add(1, std::memory_order_relaxed);
			if (value != 0)
				return WorldSyncToken{ value };
		}
	}

	void ServerWorldSyncSystem::OnClientWorldSyncResult(uint64 userId, const ClientWorldSyncResult& result, Timestamp now)
	{
		if (!m_owner) return;
		if (UserContext* resyncUser = FindUserOnCurrentShard(userId);
			resyncUser && resyncUser->worldSync.IsActive()
			&& resyncUser->worldSync.token == result.token)
		{
			auto& resync = resyncUser->worldSync;
			if (!result.succeeded || resync.deadline <= now)
			{
				FailWorldSync(*resyncUser);
				return;
			}

			if (resync.phase == eUserWorldSyncPhase::WaitingClientPrepared)
			{
				resync.phase = eUserWorldSyncPhase::WaitingClientApplied;
				SendToUser(userId, codec::MakeClientWorldCommitPacket({
					.token = resync.token,
					.correlation = { .world = resync.world, .mainRevision = resync.mainRevision },
				}), eProtocolType::TCP);
				return;
			}

			if (resync.phase == eUserWorldSyncPhase::WaitingClientApplied)
			{
				resync.phase = eUserWorldSyncPhase::WaitingTransport;
				TryCompleteWorldSync(*resyncUser);
			}
			return;
		}

		// Ignore responses from obsolete synchronization requests.
	}

	void ServerWorldSyncSystem::ResetWorldSync(UserContext& user)
	{
		user.worldSync = {};
	}

	void ServerWorldSyncSystem::BeginWorldSync(UserContext& user)
	{
		if (!m_owner || !user.IsConnected())
			return;

		if (!user.worldState.main)
		{
			ResetWorldSync(user);
			return;
		}

		if (user.worldTransition.IsActive())
		{
			user.worldSync = {
				.phase	  = eUserWorldSyncPhase::WaitingTransition,
				.deadline = NOW() + m_worldSyncTimeoutNs,
			};
			return;
		}

		const WorldRef		 world = *user.worldState.main;
		const WorldSyncToken token = IssueWorldSyncToken();

		user.worldSync = {
			.phase		  = eUserWorldSyncPhase::WaitingClientPrepared,
			.token		  = token,
			.world		  = world,
			.mainRevision = user.worldState.revision,
			.deadline	  = NOW() + m_worldSyncTimeoutNs,
		};

		SubmitWorldJob(world, [userId = user.userId](WorldBase& base)
			{
				if (auto* server = dynamic_cast<ServerWorld*>(&base))
					server->SuspendMemberReplication(userId);
			});

		SendToUser(user.userId, codec::MakeClientWorldPreparePacket({
			.token		  = token,
			.kind		  = eWorldSyncKind::WorldResync,
			.correlation  = { .world = world, .mainRevision = user.worldState.revision },
			.archetypeKey = world.instance.archetypeKey,
		}), eProtocolType::TCP);
	}

	void ServerWorldSyncSystem::TryCompleteWorldSync(UserContext& user)
	{
		if (!m_owner
			|| user.worldSync.phase != eUserWorldSyncPhase::WaitingTransport
			|| user.worldSync.retryAt > NOW()
			|| !user.IsConnected()
			|| !user.HasBothSessions()
			|| !user.worldState.Matches(user.worldSync.world, user.worldSync.mainRevision))
		{
			return;
		}

		const UserId userId = user.userId;
		const UserWorldSyncContext request = user.worldSync;
		const WorldUserContext member{
			.accountId = user.accountId, .userId = userId,
			.mainRevision = user.worldState.revision, .sessions = ResolveUserSessionBundle(user),
		};
		const auto weak = weak_from_this();
		// Mark pending before submission: same-shard routing may complete inline.
		user.worldSync.phase = eUserWorldSyncPhase::WaitingActivation;
		if (!SubmitToWorldShard(request.world.worldId, Job([userId, request, member, weak]()
			{
				auto* base = GetOrCreateWorldShardState(CurrentShardLocalChecked()).FindWorld(request.world.worldId);
				auto* world = dynamic_cast<ServerWorld*>(base);

				const bool succeeded = world
					&& world->GetWorldRef() == request.world
					&& world->ActivateMemberReplication(member);

				SubmitToUserShard(userId, Job([userId, request, succeeded, weak]()
					{
						if (const auto sync = weak.lock(); sync && sync->m_owner)
							sync->OnWorldSyncActivated(userId, request, succeeded);
					}, eJobPriority::Control));
			}, eJobPriority::Normal)))
		{
			user.worldSync.phase = eUserWorldSyncPhase::WaitingTransport;
			user.worldSync.retryAt = NOW() + 1_s;
		}
	}

	void ServerWorldSyncSystem::OnWorldSyncActivated(UserId userId, const UserWorldSyncContext& request, bool succeeded)
	{
		UserContext* user = FindUserOnCurrentShard(userId);
		if (!user
			|| user->worldSync.phase != eUserWorldSyncPhase::WaitingActivation
			|| !user->worldSync.MatchesRequest(request))
		{
			return; // Disconnected, timed out, or superseded by a newer resync.
		}

		if (user->worldSync.deadline <= NOW())
		{
			FailWorldSync(*user);
			return;
		}

		if (!user->IsConnected()
			|| !user->worldState.Matches(request.world, request.mainRevision))
		{
			return;
		}

		if (!succeeded)
		{
			user->worldSync.phase = eUserWorldSyncPhase::WaitingTransport;
			user->worldSync.retryAt = NOW() + 1_s;
			return;
		}
		// Replication now owns Created/Full delivery and baseline ACK completion.
		ResetWorldSync(*user);
	}

	void ServerWorldSyncSystem::FailWorldSync(UserContext& user)
	{
		JAM_LOG_WARN("[WorldSync] failed or timed out. userId={}, token={}, phase={}", user.userId, user.worldSync.token.value, static_cast<uint32>(user.worldSync.phase));

		ResetWorldSync(user);
		const ServerSessionBundle sessions = ResolveUserSessionBundle(user);
		const SessionRef<ServerTcpSession> tcpRef = sessions.tcp;
		if (!tcpRef.TryPost(Job([tcpRef]()
			{
				if (auto* tcp = tcpRef.TryGet(); tcp && !tcp->IsClosing())
					tcp->Disconnect();
			}, eJobPriority::Control)))
		{
			user.tcp = kInvalidSessionId;
			user.udp = kInvalidSessionId;

			if (m_owner && m_owner->GetWorldTransitionSystem())
				m_owner->GetWorldTransitionSystem()->OnDisconnected(user.userId);
		}
	}

	void ServerWorldSyncSystem::Tick(Timestamp now)
	{
		if (!m_owner) return;
		auto& users = GetOrCreateUserShardState(CurrentShardLocalChecked()).usersById;

		for (auto& entry : users.entries)
		{
			if (!entry.occupied) continue;
			if (entry.value.worldSync.IsActive() && entry.value.worldSync.deadline <= now)
				FailWorldSync(entry.value);
			else if (entry.value.worldSync.phase == eUserWorldSyncPhase::WaitingTransport)
				TryCompleteWorldSync(entry.value);
		}
	}
}
