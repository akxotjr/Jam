#include "pch.h"
#include "jamnet/core/executor/GlobalExecutor.h"
#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/core/net/Buffer.h"
#include "jamnet/core/net/Service.h"

#include "jamnet/core/net/SessionSystems.h"
#include "jamnet/core/net/TcpSession.h"
#include "jamnet/core/net/UdpSession.h"

#include <thread>
#include <chrono>

namespace jam::net
{
	SessionShardState::SessionShardState() = default;
	SessionShardState::~SessionShardState() = default;

	namespace
	{
		bool WaitPendingIocpDrain(const char* name, const std::shared_ptr<IocpObject>& object, const uint64 timeout = 250_ms)
		{
			if (!object)
				return true;

			const auto deadline = NOW_NS() + timeout;
			while (object->GetPendingDispatchCount() > 0 && NOW_NS() < deadline)
				std::this_thread::sleep_for(std::chrono::milliseconds(1));

			if (object->GetPendingDispatchCount() > 0)
			{
				JAMNET_LOG_WARN("[Service] IOCP drain timeout for {}. pending={}", name, object->GetPendingDispatchCount());
				return false;
			}

			return true;
		}
	}

	TcpSessionTable& GetTcpSessionTable(ShardLocal& L)
	{
		if (!L.sessionState)
			L.sessionState = std::make_shared<SessionShardState>();
		return L.sessionState->tcpSessions;
	}

	UdpSessionTable& GetUdpSessionTable(ShardLocal& L)
	{
		if (!L.sessionState)
			L.sessionState = std::make_shared<SessionShardState>();
		return L.sessionState->udpSessions;
	}

	DetachedTcpSessionList& GetDetachedTcpSessions(ShardLocal& L)
	{
		if (!L.sessionState)
			L.sessionState = std::make_shared<SessionShardState>();
		return L.sessionState->detachedTcpSessions;
	}

	DetachedUdpSessionList& GetDetachedUdpSessions(ShardLocal& L)
	{
		if (!L.sessionState)
			L.sessionState = std::make_shared<SessionShardState>();
		return L.sessionState->detachedUdpSessions;
	}


	Service::Service(const ServiceConfig& config) : m_config(config)
	{
	}

	Service::~Service()
	{
		Service::CloseService();
	}

	void Service::Init()
	{
		GlobalExecutor::Instance().ConveyAll(Job([this]
			{
				auto& L = CurrentShardLocalChecked();
				RegisterNetworkDomain(L);
			}));
	}

	void Service::CloseService()
	{
		m_running.store(false, std::memory_order_relaxed);

		GlobalExecutor::Instance().ConveyAll(Job([this]()
			{
				auto& L = CurrentShardLocalChecked();

				if (L.sessionState)
				{
					auto& tcp = GetTcpSessionTable(L);
					for (auto& session : tcp | std::views::values)
						if (session) session->Disconnect();
					tcp.clear();
					auto& detachedTcp = GetDetachedTcpSessions(L);
					detachedTcp.clear();
					auto& udp = GetUdpSessionTable(L);
					for (auto& session : udp | std::views::values)
						if (session) session->Disconnect();
					udp.clear();
					auto& detachedUdp = GetDetachedUdpSessions(L);
					detachedUdp.clear();
				}

				TryFinalizeDetachedSessions(L);
			}, eJobPriority::Control));

		if (m_listener)  m_listener->CloseSocket();
		if (m_udpRouter) m_udpRouter->CloseSocket();
		WaitPendingIocpDrain("TcpListener", m_listener);
		WaitPendingIocpDrain("UdpRouter", m_udpRouter);
		m_listener.reset();
		m_udpRouter.reset();
		m_iocpCore.reset();

		GlobalExecutor::Instance().ConveyAll(Job([] { FlushNetBufferThreadLocalCaches(); }));
		FlushNetBufferThreadLocalCaches();
	}


	TcpSession* Service::CreateTcpSession()
	{
		return CreateTcpSession(GetRemoteTcpNetAddress());
	}

	UdpSession* Service::CreateUdpSession()
	{
		return CreateUdpSession(GetRemoteUdpNetAddress());
	}

	TcpSession* Service::CreateTcpSession(const NetAddress& remoteAddr)
	{
		auto session = MakeTcpSession(remoteAddr);
		if (!session) return nullptr;

		TcpSession* raw = session.get();
		AdoptTcpSession(std::move(session));
		return raw;
	}

	UdpSession* Service::CreateUdpSession(const NetAddress& remoteAddr)
	{
		auto session = MakeUdpSession(remoteAddr);
		if (!session) return nullptr;

		UdpSession* raw = session.get();
		AdoptUdpSession(std::move(session));
		return raw;
	}

	std::unique_ptr<TcpSession> Service::MakeTcpSession(const NetAddress& remoteAddr)
	{
		if (!m_tcpFactory || !remoteAddr.IsValid())
			return nullptr;

		std::unique_ptr<TcpSession> session = m_tcpFactory();
		if (!session) return nullptr;

		session->SetService(this);
		session->Init(remoteAddr);

		if (m_sessionInitCallback) m_sessionInitCallback(session.get());

		return session;
	}

	std::unique_ptr<UdpSession> Service::MakeUdpSession(const NetAddress& remoteAddr)
	{
		if (!m_udpFactory || !remoteAddr.IsValid())
			return nullptr;

		std::unique_ptr<UdpSession> session = m_udpFactory();
		if (!session) return nullptr;

		session->SetService(this);
		session->Init(remoteAddr);

		if (m_sessionInitCallback) m_sessionInitCallback(session.get());

		return session;
	}

	void Service::AdoptTcpSession(std::unique_ptr<TcpSession> session)
	{
		if (!session)
			return;

		TcpSession* raw = session.get();
		const SessionTableKey key{ session->GetSocket(), session->GetRemoteNetAddress() };
		session->Post(Job([this, key, owner = std::move(session)]() mutable
			{
				auto& L = CurrentShardLocalChecked();
				auto& table = GetTcpSessionTable(L);
				if (table.emplace(key, std::move(owner)).second)
				{
					m_tcpSessionCount.fetch_add(1, std::memory_order_relaxed);
					m_sessionCount.fetch_add(1, std::memory_order_relaxed);
				}
			}, eJobPriority::Critical));
	}

	void Service::ReleaseTcpSession(TcpSession* session)
	{
		if (!session)
			return;

		const SessionTableKey key{ session->GetSocket(), session->GetRemoteNetAddress() };
		TcpSession* const sessionPtr = session;
		session->Post(Job([this, key, sessionPtr]
			{
				auto& L = CurrentShardLocalChecked();
				auto& table = GetTcpSessionTable(L);
				auto it = table.find(key);
				if (it == table.end() || it->second.get() != sessionPtr)
					return;

				auto detached = std::move(it->second);
				table.erase(it);
				GetDetachedTcpSessions(L).push_back(std::move(detached));
				m_tcpSessionCount.fetch_sub(1, std::memory_order_relaxed);
				m_sessionCount.fetch_sub(1, std::memory_order_relaxed);
				TryFinalizeDetachedSessions(L);
			}, eJobPriority::Control));
	}

	void Service::AdoptUdpSession(std::unique_ptr<UdpSession> session)
	{
		if (!session)
			return;

		const SOCKET localSocket = m_udpRouter ? reinterpret_cast<SOCKET>(m_udpRouter->GetHandle()) : INVALID_SOCKET;
		const SessionTableKey key{ localSocket, session->GetRemoteNetAddress() };
		session->Post(Job([this, key, owner = std::move(session)]() mutable
			{
				auto& L = CurrentShardLocalChecked();
				auto& table = GetUdpSessionTable(L);
				if (table.emplace(key, std::move(owner)).second)
				{
					m_udpSessionCount.fetch_add(1, std::memory_order_relaxed);
					m_sessionCount.fetch_add(1, std::memory_order_relaxed);
				}
			}, eJobPriority::Critical));
	}

	void Service::ReleaseUdpSession(UdpSession* session)
	{
		if (!session) return;

		const SOCKET localSocket = m_udpRouter ? reinterpret_cast<SOCKET>(m_udpRouter->GetHandle()) : INVALID_SOCKET;
		const SessionTableKey key{ localSocket, session->GetRemoteNetAddress() };
		UdpSession* const sessionPtr = session;
		session->Post(Job([this, key, sessionPtr]
			{
				auto& L = CurrentShardLocalChecked();
				auto& table = GetUdpSessionTable(L);
				auto it = table.find(key);
				if (it == table.end() || it->second.get() != sessionPtr)
					return;

				auto detached = std::move(it->second);
				table.erase(it);
				GetDetachedUdpSessions(L).push_back(std::move(detached));
				m_udpSessionCount.fetch_sub(1, std::memory_order_relaxed);
				m_sessionCount.fetch_sub(1, std::memory_order_relaxed);
				TryFinalizeDetachedSessions(L);
			}, eJobPriority::Control));
	}

	void Service::TryFinalizeDetachedSessions(ShardLocal& L)
	{
		if (L.sessionState)
		{
			auto& detachedTcp = GetDetachedTcpSessions(L);
			std::erase_if(detachedTcp, [](const std::unique_ptr<TcpSession>& session)
				{
					return !session || (session->IsClosing() && session->GetPendingDispatchCount() == 0);
				});
			auto& detachedUdp = GetDetachedUdpSessions(L);
			std::erase_if(detachedUdp, [](const std::unique_ptr<UdpSession>& session)
				{
					return !session || (session->IsClosing() && session->GetPendingDispatchCount() == 0);
				});
		}
	}

	bool Service::RegisterIocpObject(IocpObject* object)
	{
		IocpCore* core = GetIocpCore();
		return core && core->Register(object);
	}



	ClientService::ClientService(const ServiceConfig& config) : Service(config)
	{
		m_type = eServiceType::CLIENT;
	}

	bool ClientService::Start()
	{
		if (!HasUdpFactory())
			return false;

		m_iocpCore = GlobalExecutor::Instance().AcquireIocpCore();
		if (!m_iocpCore)
			return false;
		m_running.store(true, std::memory_order_relaxed);

		m_udpRouter = std::make_shared<UdpRouter>();
		if (m_udpRouter->Start(this) == false)
		{
			CloseService();
			return false;
		}

		return true;
	}



	ServerService::ServerService(const ServiceConfig& config) : Service(config)
	{
		m_type = eServiceType::SERVER;
	}

	bool ServerService::Start()
	{
		if (!CanStart())
			return false;

		m_iocpCore = GlobalExecutor::Instance().AcquireIocpCore();
		if (!m_iocpCore)
			return false;
		m_running.store(true, std::memory_order_relaxed);

		bool startedAny = false;

		if (HasTcpFactory() && GetLocalTcpNetAddress().IsValid())
		{
			m_listener = std::make_shared<TcpListener>();
			if (m_listener->StartAccept(this) == false)
			{
				CloseService();
				return false;
			}
			startedAny = true;
		}

		if (HasUdpFactory() && GetLocalUdpNetAddress().IsValid())
		{
			m_udpRouter = std::make_shared<UdpRouter>();
			if (m_udpRouter->Start(this) == false)
			{
				CloseService();
				return false;
			}
			startedAny = true;
		}

		if (!startedAny)
		{
			CloseService();
			return false;
		}

		return true;
	}
}
