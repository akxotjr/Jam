#include "pch.h"
#include "jamnet/core/executor/GlobalExecutor.h"
#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/core/net/Buffer.h"
#include "jamnet/core/net/Service.h"

#include "jamnet/core/net/TcpSession.h"
#include "jamnet/core/net/UdpSession.h"

#include <thread>
#include <chrono>

namespace jam::net
{


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



	Service::Service(const ServiceConfig& config) : m_config(config)
	{
	}

	Service::~Service()
	{
		Service::CloseService();
	}

	void Service::Init()
	{
	}

	void Service::CloseService()
	{
		m_running.store(false, std::memory_order_relaxed);

		GlobalExecutor::Instance().ConveyAll(Job([this]()
			{
				auto& L = CurrentShardLocalChecked();

				if (L.sessionState)
				{
					auto& prebound = GetPreboundSessionTable(L);
					for (auto it = prebound.begin(); it != prebound.end();)
					{
						Session* session = it->second.get();
						if (session && session->GetService() == this)
						{
							session->Disconnect();
							it = prebound.erase(it);
							continue;
						}
						++it;
					}

					for (auto& entry : L.sessionState->sessionsById.entries)
					{
						if (!entry.object || entry.object->GetService() != this)
							continue;

						entry.object->Disconnect();
						entry.object.reset();
						entry.state = eShardOwnedObjectState::Destroyed;
						L.sessionState->sessionsById.freeIndices.push_back(entry.index);
					}
				}
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


	std::unique_ptr<TcpSession> Service::CreateTcpSession()
	{
		return CreateTcpSession(GetRemoteTcpNetAddress());
	}

	std::unique_ptr<UdpSession> Service::CreateUdpSession()
	{
		return CreateUdpSession(GetRemoteUdpNetAddress());
	}

	std::unique_ptr<TcpSession> Service::CreateTcpSession(const NetAddress& remoteAddr)
	{
		return MakeTcpSession(remoteAddr);
	}

	std::unique_ptr<UdpSession> Service::CreateUdpSession(const NetAddress& remoteAddr)
	{
		return MakeUdpSession(remoteAddr);
	}

	std::unique_ptr<TcpSession> Service::MakeTcpSession(const NetAddress& remoteAddr)
	{
		if (!m_tcpFactory || !remoteAddr.IsValid())
			return nullptr;

		std::unique_ptr<TcpSession> session = m_tcpFactory();
		if (!session) return nullptr;

		session->SetService(this);
		if (m_sessionInitCallback) m_sessionInitCallback(session.get());
		session->Init(remoteAddr);

		return session;
	}

	std::unique_ptr<UdpSession> Service::MakeUdpSession(const NetAddress& remoteAddr)
	{
		if (!m_udpFactory || !remoteAddr.IsValid())
			return nullptr;

		std::unique_ptr<UdpSession> session = m_udpFactory();
		if (!session) return nullptr;

		session->SetService(this);
		if (m_sessionInitCallback) m_sessionInitCallback(session.get());
		session->Init(remoteAddr);

		return session;
	}

	void Service::NotifyTcpSessionAttached()
	{
		m_tcpSessionCount.fetch_add(1, std::memory_order_relaxed);
		m_sessionCount.fetch_add(1, std::memory_order_relaxed);
	}

	void Service::ReleaseTcpSession(TcpSession* session)
	{
		if (!session)
			return;

		TcpSession* const sessionPtr = session;
		session->Post(Job([this, sessionPtr]
			{
				auto& L = CurrentShardLocalChecked();
				std::unique_ptr<Session> detached = {};

				if (sessionPtr->GetSessionId() == kInvalidSessionId)
				{
					const EndpointHandle handle = sessionPtr->GetEndpointHandle();
					auto& table = GetPreboundSessionTable(L);
					auto it = table.find(handle);
					if (it == table.end() || it->second.get() != sessionPtr)
						return;
					detached = std::move(it->second);
					table.erase(it);
				}
				else
				{
					detached = L.sessionState->ReleaseBoundSession(sessionPtr->GetSessionId(), sessionPtr);
					if (!detached)
						return;
					L.sessionState->FreeSessionId(sessionPtr->GetSessionId());
					detached->FinalizeShardOwnedClose();
				}

				m_tcpSessionCount.fetch_sub(1, std::memory_order_relaxed);
				m_sessionCount.fetch_sub(1, std::memory_order_relaxed);
			}, eJobPriority::Control));
	}

	void Service::NotifyUdpSessionAttached(uint64 endpointId, RouteKey routeKey)
	{
		if (m_udpRouter)
			m_udpRouter->RegisterIngressPrebindRoute(endpointId, routeKey);

		m_udpSessionCount.fetch_add(1, std::memory_order_relaxed);
		m_sessionCount.fetch_add(1, std::memory_order_relaxed);
	}

	void Service::ReleaseUdpSession(UdpSession* session)
	{
		if (!session) return;

		const uint64 endpointId = session->GetEndpointId();
		UdpSession* const sessionPtr = session;
		session->Post(Job([this, endpointId, sessionPtr]
			{
				auto& L = CurrentShardLocalChecked();
				std::unique_ptr<Session> detached = {};

				if (sessionPtr->GetSessionId() == kInvalidSessionId)
				{
					const EndpointHandle handle = sessionPtr->GetEndpointHandle();
					auto& table = GetPreboundSessionTable(L);
					auto it = table.find(handle);
					if (it == table.end() || it->second.get() != sessionPtr)
						return;
					detached = std::move(it->second);
					table.erase(it);
				}
				else
				{
					detached = L.sessionState->ReleaseBoundSession(sessionPtr->GetSessionId(), sessionPtr);
					if (!detached)
						return;
					L.sessionState->FreeSessionId(sessionPtr->GetSessionId());
					detached->FinalizeShardOwnedClose();
				}

				if (m_udpRouter)
					m_udpRouter->ClearIngressRoute(endpointId);

				m_udpSessionCount.fetch_sub(1, std::memory_order_relaxed);
				m_sessionCount.fetch_sub(1, std::memory_order_relaxed);
			}, eJobPriority::Control));
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
