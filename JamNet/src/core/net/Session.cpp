#include "pch.h"
#include "jamnet/core/net/Session.h"

#include "jamnet/core/executor/GlobalExecutor.h"
#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/core/net/Service.h"
#include "jamnet/core/net/SessionSystems.h"
#include "jamnet/core/net/SocketUtils.h"

namespace jam::net
{
	void Session::Init(const NetAddress& remoteAddr)
	{
		if (!remoteAddr.IsValid())
			return;
		SetRemoteNetAddress(remoteAddr);
		m_endpointId = MakeEndpointId(remoteAddr);

		if (IsClientSide() && m_accountId != 0)
			m_key = GLOBAL_EXEC.MakeAffinityRouteKey(m_accountId);
		else
			m_key = IsTcp() ? MakeTcpRouteKey(remoteAddr) : MakeUdpRouteKey(remoteAddr);

		if (!IsValidRouteKey(m_key))
			return;

		const auto shard = GLOBAL_EXEC.GetShard(m_key);
		if (!shard) return;

		m_shard = shard;
	}

	bool Session::BeginClose(eMailboxCloseMode mode, std::function<void()> onClosed)
	{
		(void)mode;
		m_onShardOwnedClosed = std::move(onClosed);
		Disconnect();
		return true;
	}

	void Session::SetSocket(SOCKET socket)
	{
		SocketUtils::Close(m_socket);
		m_socket = socket;
	}

	std::shared_ptr<Service> Session::GetServiceRef() const
	{
		return m_service ? m_service->shared_from_this() : nullptr;
	}

	void Session::Post(Job j) const
	{
		if (m_closed.load(std::memory_order_acquire))
		{
			JAM_LOG_WARN("[Session::Post] account id= {} mailbox is already closed", m_accountId);
			return;
		}
		// Pre-bind sessions do not own a mailbox yet. Route these jobs through
		// the owning shard so connect/bind/rehome work before sessionId issuance.
		if (m_sessionId == kInvalidSessionId)
		{
			if (auto shard = m_shard.lock())
			{
				shard->Submit(std::move(j));
				return;
			}
		}

		if (!m_mailboxRef.TryPost(std::move(j)))
		{
			JAM_LOG_WARN("[Session::Post] failed trying post job. accountId={}, userId={}, sessionId={}, mailboxValid={}, closing={}",
				m_accountId, m_userId, m_sessionId, m_mailboxRef.IsValid(), IsClosing());
		}
	}

	void Session::Submit(Job j) const
	{
		if (auto shard = m_shard.lock())
			shard->Submit(std::move(j));
	}

	void Session::SubmitAfter(Job j, uint64 delay_ns) const
	{
		if (auto shard = m_shard.lock())
			shard->SubmitAfter(std::move(j), delay_ns);
	}

	bool Session::IsCurrentShardContext() const
	{
		const auto* local = CurrentShardLocal();
		auto shard = m_shard.lock();
		return local && shard && local->shardIndex == static_cast<uint32>(shard->GetIndex());
	}

	void Session::CreateEntity()
	{
		if (m_entity != entt::null)
			return;

		auto& L = CurrentShardLocalChecked();
		m_entity = L.registry.create();

		BootstrapSessionEntity(L, m_entity, this);
	}

	void Session::CompleteClose()
	{
		m_closed.store(true, std::memory_order_release);

		if (m_onShardOwnedClosed)
		{
			auto onClosed = std::move(m_onShardOwnedClosed);
			m_onShardOwnedClosed = {};
			onClosed();
		}
	}

	void Session::CompleteProtocolDisconnect()
	{
		if (m_protocolDisconnectCompleted.exchange(true, std::memory_order_acq_rel))
			return;

		// Disconnect observers may start cross-shard teardown. Reject transport work
		// before they remove world/user/session relationships.
		MarkClosing();
		m_connected.store(false, std::memory_order_relaxed);
		m_isReady = false;
		m_clientBind.active = false;
		++m_clientBind.timerToken;
		if (m_sessionEstablished)
		{
			m_sessionEstablished = false;
			OnSessionReleased();
		}
		m_releaseQueued.store(true, std::memory_order_release);
		if (GetPendingDispatchCount() == 0)
			OnPendingDispatchDrained();
	}

	void Session::CompleteSessionEstablishment(bool ready)
	{
		if (m_sessionEstablished || IsClosing() || m_sessionId == kInvalidSessionId || m_entity == entt::null)
			return;

		m_connected.store(true, std::memory_order_release);
		m_isReady = ready;
		m_sessionEstablished = true;
		OnSessionEstablished();
	}

	bool Session::AdoptAuthoritativeSessionId(SessionId sessionId)
	{
		if (!IsClientSide() || sessionId == kInvalidSessionId || m_sessionId != kInvalidSessionId
			|| m_userId == kInvalidRuntimeId)
			return false;

		const auto shard = m_shard.lock();
		if (!shard)
			return false;

		m_sessionId = sessionId;
		m_mailboxRef = shard->CreateMailboxRef(m_sessionId);
		if (!m_mailboxRef.IsValid())
		{
			m_sessionId = kInvalidSessionId;
			return false;
		}

		if (GetEntity() == entt::null)
			CreateEntity();

		return true;
	}






	EndpointId Session::MakeEndpointId(const NetAddress& addr)
	{
		const uint64 ip   = addr.GetIpAddressU64();
		const uint64 port = addr.GetPort();

		uint64 value = ip | (port << 32);
		value ^= value >> 33;
		value *= 0xff51afd7ed558ccdULL;
		value ^= value >> 33;
		value *= 0xc4ceb9fe1a85ec53ULL;
		value ^= value >> 33;
		return value != 0 ? value : 1;
	}

	RouteKey Session::MakeTcpRouteKey(const NetAddress& remoteAddr)
	{
		return GLOBAL_EXEC.MakeRouteKey(kTcpSessionRouteDomain, MakeEndpointId(remoteAddr));
	}

	RouteKey Session::MakeUdpRouteKey(const NetAddress& remoteAddr)
	{
		return GLOBAL_EXEC.MakeRouteKey(kUdpSessionRouteDomain, MakeEndpointId(remoteAddr));
	}
}
