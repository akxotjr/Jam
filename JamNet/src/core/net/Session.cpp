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

	bool Session::MatchesEndpointHandle(const EndpointHandle& handle) const
	{
		return handle.IsValid() && m_endpointId == handle.endpointId && m_key == handle.routeKey;
	}

	std::shared_ptr<Service> Session::GetServiceRef() const
	{
		return m_service ? m_service->shared_from_this() : nullptr;
	}

	void Session::Post(Job j) const
	{
		if (m_closed.load(std::memory_order_acquire))
		{
			JAMNET_LOG_WARN("[Session::Post] account id= {} mailbox is already closed", m_accountId);
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
			JAMNET_LOG_WARN("[Session::Post] failed trying post job");
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

	void Session::Rehome(RouteKey newRouteKey, std::function<void(bool)> onDone)
	{
		auto* service = GetService();
		if (!service || !IsValidRouteKey(newRouteKey))
		{
			if (onDone) onDone(false);
			return;
		}

		const EndpointHandle oldHandle = GetEndpointHandle();
		if (!oldHandle.IsValid())
		{
			if (onDone) onDone(false);
			return;
		}

		if (oldHandle.routeKey == newRouteKey)
		{
			if (onDone) onDone(true);
			return;
		}

		auto targetShard = GLOBAL_EXEC.GetShard(newRouteKey);
		if (!targetShard)
		{
			if (onDone) onDone(false);
			return;
		}

		const EndpointHandle endpointHandle = GetEndpointHandle();

		Submit(Job([this, endpointHandle, newRouteKey, targetShard, onDone = std::move(onDone)]() mutable
			{
				auto& oldLocal = CurrentShardLocalChecked();

				auto& oldTable = GetPreboundSessionTable(oldLocal);
				auto it = oldTable.find(endpointHandle);
				if (it == oldTable.end() || it->second.get() != this)
				{
					if (onDone)
						onDone(false);
					return;
				}
				std::unique_ptr<Session> moved = std::move(it->second);
				oldTable.erase(it);

				if (m_entity != entt::null)
				{
					if (oldLocal.registry.valid(m_entity))
						oldLocal.registry.destroy(m_entity);
					m_entity = entt::null;
				}

				m_key	= newRouteKey;
				m_shard	= targetShard;
				const EndpointHandle newHandle{ newRouteKey, endpointHandle.endpointId };
				const bool movedIsUdp = moved && moved->IsUdp();
				targetShard->Submit(Job([newHandle, newRouteKey, movedIsUdp, owner = std::move(moved), onDone = std::move(onDone)]() mutable
					{
						auto& newLocal = CurrentShardLocalChecked();
						auto& newTable = GetPreboundSessionTable(newLocal);
						Service* const service = owner ? owner->GetService() : nullptr;
						const bool ok = owner && newTable.emplace(newHandle, std::move(owner)).second;

						if (ok && movedIsUdp)
						{
							if (service && service->m_udpRouter)
								service->m_udpRouter->RegisterIngressPrebindRoute(newHandle.endpointId, newRouteKey);
						}

						if (onDone) onDone(ok);
					}, eJobPriority::Critical));
			}, eJobPriority::Critical));
	}

	void Session::CreateEntity()
	{
		if (m_entity != entt::null)
			return;

		auto& L = CurrentShardLocalChecked();
		m_entity = L.registry.create();

		BootstrapSessionEntity(L, m_entity, this);
		NotifyLinkEstablishedIfReady();
	}

	bool Session::TryBeginServerBind()
	{
		if (m_sessionId != kInvalidSessionId || m_state.load(std::memory_order_relaxed) == eSessionState::Binding)
			return false;

		SetSessionState(eSessionState::Binding);
		return true;
	}

	void Session::EndServerBind()
	{
		if (m_sessionId == kInvalidSessionId && m_state.load(std::memory_order_relaxed) == eSessionState::Binding)
			SetSessionState(eSessionState::Connected);
	}

	void Session::NotifyLinkEstablishedIfReady()
	{
		if (m_linkEstablishedNotified)
			return;
		if (m_sessionId == kInvalidSessionId || m_entity == entt::null)
			return;

		SetSessionState(eSessionState::Ready);
		m_linkEstablishedNotified = true;
		OnLinkEstablished();
	}

	void Session::NotifyLinkTerminatedIfEstablished()
	{
		if (!m_linkEstablishedNotified)
			return;

		m_linkEstablishedNotified = false;
		if (m_sessionId != kInvalidSessionId)
			SetSessionState(eSessionState::Bound);
		OnLinkTerminated();
	}

	void Session::NotifyDisconnectedOnce()
	{
		if (!m_disconnectedNotified.exchange(true, std::memory_order_acq_rel))
			OnDisconnected();
	}

	void Session::FinalizeShardOwnedClose()
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
		SetSessionState(eSessionState::Disconnected);
		m_clientBind.active = false;
		++m_clientBind.timerToken;
		NotifyLinkTerminatedIfEstablished();
		NotifyDisconnectedOnce();

		m_releaseQueued.store(true, std::memory_order_release);
		if (GetPendingDispatchCount() == 0)
			OnPendingDispatchDrained();
	}

	void Session::IssueSessionId()
	{
		if (!IsServerSide())
			return;
		if (m_sessionId != kInvalidSessionId || m_userId == kInvalidRuntimeId)
			return;

		auto& state = GetOrCreateSessionShardState(CurrentShardLocalChecked());
		m_sessionId = state.AllocSessionId();
		if (m_sessionId == kInvalidSessionId)
			return;

		auto owner = state.DetachPreboundSession(GetEndpointHandle());
		if (!owner)
		{
			state.FreeSessionId(m_sessionId);
			m_sessionId = kInvalidSessionId;
			return;
		}

		if (!state.PromotePreboundSession(m_sessionId, owner))
		{
			state.AttachPreboundSession(std::move(owner));
			state.FreeSessionId(m_sessionId);
			m_sessionId = kInvalidSessionId;
			return;
		}

		if (auto shard = m_shard.lock())
			m_mailboxRef = shard->CreateMailboxRef(m_sessionId);

		if (IsUdp())
		{
			if (auto* service = GetService(); service && service->m_udpRouter)
				service->m_udpRouter->PromoteIngressToBound(m_endpointId, m_sessionId);
		}

		SetSessionState(eSessionState::Bound);
		if (GetEntity() == entt::null)
			CreateEntity();

		NotifyLinkEstablishedIfReady();
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

		SetSessionState(eSessionState::Bound);
		if (GetEntity() == entt::null)
			CreateEntity();

		NotifyLinkEstablishedIfReady();
		return true;
	}






	uint64 Session::MakeEndpointId(const NetAddress& addr)
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
