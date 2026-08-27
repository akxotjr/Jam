#include "pch.h"
#include "jamnet/core/executor/GlobalExecutor.h"
#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/runtime/world/lifecycle/WorldBase.h"
#include "jamnet/runtime/session/ServerSession.h"

namespace jam::net
{
	namespace
	{
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

	bool WorldBase::Initialize()
	{
		if (!m_config.HasWorld())
			return false;
		if (m_alive.load(std::memory_order_acquire) || m_mailboxRef.IsValid() || !m_shard.expired())
			return false;

		const auto* local = CurrentShardLocal();
		if (!local)
			return false;

		auto shard = GLOBAL_EXEC.GetShardFromIndex(local->shardIndex);
		if (!shard)
			return false;
		m_shard = shard;

		m_mailboxRef = shard->CreateMailboxRef(GetWorldId());
		if (!m_mailboxRef.IsValid())
			return false;

		if (!OnInitialize())
		{
			const uint32 mailboxId = m_mailboxRef.mailbox->GetId();
			m_mailboxRef = {};
			m_shard.reset();
			shard->CloseMailbox(mailboxId, eMailboxCloseMode::Abort, {});
			return false;
		}

		m_alive.store(true, std::memory_order_release);

		return true;
	}

	bool WorldBase::BeginClose(eMailboxCloseMode mode, std::function<void()> onClosed)
	{
		if (!m_alive.exchange(false, std::memory_order_acq_rel))
		{
			if (onClosed)
				onClosed();
			return true;
		}

		JAM_ASSERT(IsCurrentShardContext());
		OnCloseStarted();

		// Client worlds are shared-owned by ClientMainWorldState. Keep an optional
		// lease until the asynchronous mailbox close callback has completed.
		auto lifetime = weak_from_this().lock();

		auto finalize = [this, lifetime = std::move(lifetime), onClosed = std::move(onClosed)]() mutable
			{
				m_mailboxRef = {};
				m_shard.reset();
				OnCloseCompleted();
				lifetime.reset();

				if (onClosed)
					onClosed();
			};

		auto close = [this, finalize = std::move(finalize)]() mutable
			{
				BeginCloseBarrier(std::move(finalize));
			};

		auto shard = m_shard.lock();
		if (!shard || !m_mailboxRef.IsValid()) 
		{
			close();
			return true;
		}

		shard->CloseMailbox(m_mailboxRef.mailbox->GetId(), mode, [close = std::move(close)]() mutable { close(); });
		return true;
	}

	RuntimeId WorldBase::GetShardOwnedRuntimeId() const
	{
		return m_config.world.worldId;
	}

	MailboxRef WorldBase::GetShardOwnedMailboxRef() const
	{
		return GetMailboxRef();
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
		auto shard = m_shard.lock();
		return local && shard && local->shardIndex == static_cast<uint32>(shard->GetIndex());
	}

	MailboxRef WorldBase::GetMailboxRef() const
	{
		return m_mailboxRef;
	}







	bool WorldMembershipHost::OnInitialize()
	{
		if (!WorldBase::OnInitialize())
			return false;

		m_userContexts.clear();
		return true;
	}

	bool WorldMembershipHost::AddMember(WorldUserContext user)
	{
		JAM_ASSERT(IsCurrentShardContext());

		if (user.userId == kInvalidUserId)
			return false;

		auto [_, inserted] = m_userContexts.try_emplace(user.userId, user);
		if (!inserted)
			return false;

		OnUserEntered(user.userId);
		return true;
	}

	bool WorldMembershipHost::RemoveMember(UserId userId)
	{
		JAM_ASSERT(IsCurrentShardContext());

		if (userId == kInvalidUserId)
			return false;

		if (!m_userContexts.erase(userId))
			return false;

		OnUserLeft(userId);
		return true;
	}

	void WorldMembershipHost::UpdateMemberContext(WorldUserContext user)
	{
		JAM_ASSERT(IsCurrentShardContext());

		if (user.userId == kInvalidUserId)
			return;

		if (auto it = m_userContexts.find(user.userId); it != m_userContexts.end())
			it->second = std::move(user);
	}

	Session* WorldMembershipHost::GetMemberSession(uint64 userId, eProtocolType protocol)
	{
		auto it = m_userContexts.find(userId);
		if (it == m_userContexts.end())
			return nullptr;

		return SelectWorldSession(it->second.sessions, protocol);
	}
}
