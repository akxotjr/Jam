#include "pch.h"
#include "jamnet/core/executor/GlobalExecutor.h"
#include "jamnet/runtime/world/WorldBase.h"

#include "jamnet/core/executor/ShardInvoke.h"
#include "jamnet/runtime/ServerSession.h"
#include "jamnet/core/executor/ThreadContext.h"

namespace jam::net
{
	namespace
	{
		uint16 ResolveWorldDispatchShardIndex(const WorldBase& world)
		{
			if (world.GetLocalWorldId() != kInvalidLocalWorldId)
				return GetLocalWorldShardIndex(world.GetLocalWorldId());
			return GetWorldShardIndex(world.GetWorldId());
		}

		Session* SelectWorldSession(const ServerSessionBundle& sessions, eProtocolType protocol)
		{
			if (protocol == eProtocolType::TCP)
				return sessions.TryGetTcp();

			if (protocol == eProtocolType::UDP)
				return sessions.TryGetUdp();

			return nullptr;
		}
	}

	WorldBase::WorldBase(const WorldConfig& config)
		: m_config(config)
	{
	}

	bool WorldBase::Init()
	{
		if (!m_config.key.IsIssued())
			return false;

		RouteAssignment route{};
		std::shared_ptr<ShardExecutor> shard;
		if (m_localWorldId != kInvalidLocalWorldId)
		{
			shard = GLOBAL_EXEC.GetShardFromIndex(GetLocalWorldShardIndex(m_localWorldId));
		}
		else
		{
			route = GLOBAL_EXEC.PlaceRoute(GLOBAL_EXEC.MakeRouteKey("World", m_config.key.worldId), {});
			if (!IsValidRouteAssignment(route))
				return false;
			shard = GLOBAL_EXEC.GetShard(route);
		}

		if (!shard)
		{
			if (IsValidRouteAssignment(route))
				GLOBAL_EXEC.ReleaseRoute(route);
			return false;
		}
		m_shard = shard;
		
		if (m_localWorldId == kInvalidLocalWorldId)
		{
			m_localWorldId = InvokeOnShard(shard, [](ShardLocal& local) -> LocalWorldId
				{
					auto& state = GetOrCreateWorldShardState(local);
					return state.AllocLocalWorldId();
				}, eJobPriority::Control);
		}

		if (m_localWorldId == kInvalidLocalWorldId)
		{
			if (IsValidRouteAssignment(route))
				GLOBAL_EXEC.ReleaseRoute(route);
			return false;
		}

		m_mailboxRef = shard->CreateMailboxRef(m_localWorldId);
		if (!m_mailboxRef.IsValid())
			return false;

		m_alive.store(true, std::memory_order_relaxed);

		return true;
	}

	void WorldBase::Shutdown(eMailboxCloseMode mode, std::function<void()> onClosed)
	{
		m_alive.store(false, std::memory_order_relaxed);

		auto close = [this, onClosed = std::move(onClosed)]
			{
				m_mailboxRef = {};
				m_shard.reset();
				GLOBAL_EXEC.ReleaseRoute(ResolveWorldDispatchShardIndex(*this));

				FinalizeShutdown();

				if (onClosed) onClosed();
			};

		auto shard = m_shard.lock();
		if (!shard || !m_mailboxRef.IsValid()) 
		{
			close();
			return;
		}

		shard->CloseMailbox(m_mailboxRef.mailbox->GetId(), mode, [this, close = std::move(close)]() mutable { close(); });
	}

	RuntimeId WorldBase::GetShardOwnedRuntimeId() const
	{
		return m_localWorldId != kInvalidLocalWorldId
			? static_cast<RuntimeId>(m_localWorldId)
			: static_cast<RuntimeId>(m_config.key.worldId);
	}

	MailboxRef WorldBase::GetShardOwnedMailboxRef() const
	{
		return GetMailboxRef();
	}

	bool WorldBase::BeginClose(eMailboxCloseMode mode, std::function<void()> onClosed)
	{
		Shutdown(mode, std::move(onClosed));
		return true;
	}

	void WorldBase::Submit(Job j) const
	{
		JAM_ASSERT(IsCurrentShardContext());
		if (auto shard = m_shard.lock())
			shard->Submit(std::move(j));
	}
	
	bool WorldBase::Post(Job j) const
	{
		JAM_ASSERT(IsCurrentShardContext());
		return m_mailboxRef.TryPost(std::move(j));
	}

	bool WorldBase::IsCurrentShardContext() const
	{
		const auto* local = CurrentShardLocal();
		return local && local->shardIndex == ResolveWorldDispatchShardIndex(*this);
	}

	MailboxRef WorldBase::GetMailboxRef() const
	{
		return m_mailboxRef;
	}







	bool WorldMembershipHost::Init()
	{
		if (!WorldBase::Init())
			return false;

		m_userContexts.clear();
		m_users.clear();
		return true;
	}

	bool WorldMembershipHost::AddMember(WorldUserContext user)
	{
		JAM_ASSERT(IsCurrentShardContext());

		if (user.userId == 0)
			return false;

		if (std::ranges::find(m_users, user.userId) == m_users.end())
			m_users.push_back(user.userId);

		user.joined = true;
		m_userContexts[user.userId] = user;

		OnUserJoined(user.userId);
		return true;
	}

	bool WorldMembershipHost::RemoveMember(uint64 userId)
	{
		JAM_ASSERT(IsCurrentShardContext());

		if (userId == 0)
			return false;

		std::erase(m_users, userId);
		m_userContexts.erase(userId);

		OnUserLeft(userId);
		return true;
	}

	void WorldMembershipHost::UpdateMemberContext(WorldUserContext user)
	{
		JAM_ASSERT(IsCurrentShardContext());

		if (user.userId == 0)
			return;

		auto& current = m_userContexts[user.userId];
		const bool joined = current.joined;
		current = user;
		current.joined = joined;
	}

	void WorldMembershipHost::RemoveMemberContext(uint64 userId)
	{
		JAM_ASSERT(IsCurrentShardContext());

		if (userId == 0)
			return;

		m_userContexts.erase(userId);
	}

	Session* WorldMembershipHost::GetMemberSession(uint64 userId, eProtocolType protocol)
	{
		auto it = m_userContexts.find(userId);
		if (it == m_userContexts.end())
			return nullptr;

		return SelectWorldSession(it->second.sessions, protocol);
	}
}
