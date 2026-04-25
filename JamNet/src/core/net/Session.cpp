#include "pch.h"
#include "jamnet/core/net/Session.h"

#include "jamnet/core/executor/GlobalExecutor.h"
#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/core/net/Service.h"
#include "jamnet/core/net/SessionSystems.h"
#include "jamnet/core/net/SocketUtils.h"

#include "jamnet/core/net/TcpSession.h"
#include "jamnet/core/net/UdpSession.h"

namespace jam::net
{
	Session* FindSessionByHandle(ShardLocal& L, const SessionHandle& handle)
	{
		if (!handle.IsValid())
			return nullptr;

		if (L.sessionState)
		{
			auto& tcp = GetTcpSessionTable(L);
			for (auto& session : tcp | std::views::values)
			{
				if (session && session->MatchesSessionHandle(handle))
					return session.get();
			}
		}

		if (L.sessionState)
		{
			auto& udp = GetUdpSessionTable(L);
			for (auto& session : udp | std::views::values)
			{
				if (session && session->MatchesSessionHandle(handle))
					return session.get();
			}
		}

		return nullptr;
	}

	const Session* FindSessionByHandle(const ShardLocal& L, const SessionHandle& handle)
	{
		return FindSessionByHandle(const_cast<ShardLocal&>(L), handle);
	}

	std::atomic<uint64> Session::s_sessionIdGenerator{ 1 };

	Session::Session()
	{
		m_sessionId = s_sessionIdGenerator.fetch_add(1, std::memory_order_relaxed);
	}

	void Session::Init(const NetAddress& remoteAddr)
	{
		JAM_ASSERT_OR_RETURN(remoteAddr.IsValid());
		SetRemoteNetAddress(remoteAddr);

		m_key = IsTcp() ? MakeTcpRouteKey(remoteAddr) : MakeUdpRouteKey(remoteAddr);
		JAM_ASSERT_OR_RETURN(IsValidRouteKey(m_key));

		m_boundShard = GLOBAL_EXEC.GetShard(m_key);
		auto shard = m_boundShard.lock();
		JAM_ASSERT_OR_RETURN(shard);

		m_mailbox = shard->CreateMailbox();
		JAM_ASSERT_OR_RETURN(m_mailbox);
	}

	bool Session::IsServerSide() const
	{
		if (m_service)
			return m_service->GetServiceType() == eServiceType::SERVER;
		return false;
	}

	bool Session::IsClientSide() const
	{
		if (m_service)
			return m_service->GetServiceType() == eServiceType::CLIENT;
		return false;
	}

	void Session::SetService(Service* service)
	{
		m_service = service;
	}


	void Session::SetSocket(SOCKET socket)
	{
		SocketUtils::Close(m_socket);
		m_socket = socket;
	}

	bool Session::MatchesSessionHandle(const SessionHandle& handle) const
	{
		return handle.IsValid() && m_sessionId == handle.sessionId && m_key == handle.routeKey;
	}

	void Session::Post(Job j) const
	{
		if (m_closed.load(std::memory_order_acquire))
			return;

		const_cast<Session*>(this)->EnsureBound();

		if (m_mailbox)
			m_mailbox->Post(std::move(j));
	}

	void Session::Submit(Job j) const
	{
		if (auto shard = m_boundShard.lock())
			shard->Submit(std::move(j));
	}

	void Session::SubmitAfter(Job j, uint64 delay_ns) const
	{
		if (auto shard = m_boundShard.lock())
			shard->SubmitAfter(std::move(j), delay_ns);
	}






	void Session::EnsureBound()
	{
		if (!m_boundShard.expired() && m_entityReady.load(std::memory_order_acquire))
			return;

		auto shard = GLOBAL_EXEC.GetShard(m_key);
		if (!shard) return;
		if (m_boundShard.expired()) m_boundShard = shard;
		if (!m_mailbox) m_mailbox = shard->CreateMailbox();

		if (m_entityReady.load(std::memory_order_acquire)) return;

		bool expected = false;
		if (!m_entityCreating.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
			return;

		const SessionHandle handle = GetSessionHandle();
		shard->Submit(Job([handle]()
			{
				auto& L = CurrentShardLocalChecked();
				if (auto* session = FindSessionByHandle(L, handle))
					session->CreateEntity();
			}, eJobPriority::Critical));
	}

	void Session::CreateEntity()
	{
		if (m_entityReady.load(std::memory_order_acquire))
		{
			m_entityCreating.store(false, std::memory_order_release);
			return;
		}

		auto& L = CurrentShardLocalChecked();
		auto& R = L.registry;

		if (m_entity == entt::null)
			m_entity = R.create();

		const entt::entity e = m_entity;

		BootstrapSessionEntity(L, e, this);
		OnEntityCreated(R, e);

		m_entityReady.store(true, std::memory_order_release);
		m_entityCreating.store(false, std::memory_order_release);
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
