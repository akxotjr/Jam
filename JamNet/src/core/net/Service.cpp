#include "pch.h"
#include "jamnet/core/net/Service.h"
#include "jamnet/core/net/SessionSystems.h"

namespace jam::net
{
	Service::Service(const ServiceConfig& config) : m_config(config)
	{
		m_iocpCore = make_unique<IocpCore>();
	}

	Service::~Service()
	{
		Service::CloseService();
	}

	void Service::Init()
	{
		GlobalExecutor::Instance().ConveyAll(Job([this]
			{
				auto& L = SHARD_LOCAL_CHECKED();
				RegisterNetworkDomain(L);
			}));
	}



	void Service::CloseService()
	{
		GlobalExecutor::Instance().CancelPerioidc(m_periodicHandle);


		vector<shared_ptr<TcpSession>> tcp;
		vector<shared_ptr<UdpSession>> udp;
		{
			READ_LOCK
			tcp.reserve(m_tcpSessions.size());
			for (auto& val : m_tcpSessions | std::views::values)
				if (val) tcp.push_back(val);
			udp.reserve(m_udpSessions.size());
			for (auto& val : m_udpSessions | std::views::values)
				if (val) udp.push_back(val);
		}

		for (auto& s : tcp) s->Disconnect();
		for (auto& s : udp) s->Disconnect();

		{
			WRITE_LOCK
			m_handshakingUdpSessions.clear();
		}

		if (m_listener) m_listener->CloseSocket();
		if (m_udpRouter) m_udpRouter->CloseSocket();

		m_running.store(false, std::memory_order_relaxed);
	}


	shared_ptr<Session> Service::CreateSession(eProtocolType protocol)
	{
		shared_ptr<Session> session = nullptr;

		switch (protocol)
		{
		case eProtocolType::TCP:
			if (!m_tcpSessionFactory) return nullptr;
			session = m_tcpSessionFactory();
			if (!session) return nullptr;
			if (m_iocpCore->Register(session) == false) return nullptr;
			session->SetRemoteNetAddress(GetRemoteTcpNetAddress());
			break;

		case eProtocolType::UDP:
			if (!m_udpSessionFactory) return nullptr;
			session = m_udpSessionFactory();
			if (!session) return nullptr;
			session->SetRemoteNetAddress(GetRemoteUdpNetAddress());
			break;

		default:break;
		}

		session->SetService(this);
		session->Init();

		if (m_sessionInitCallback) m_sessionInitCallback(session);

		return session;
	}

	void Service::RegisterTcpSession(const std::shared_ptr<TcpSession>& session)
	{
		WRITE_LOCK

		auto addr = session->GetRemoteNetAddress();
		if (m_tcpSessions.contains(addr))
			return;

		m_tcpSessionCount++;
		m_tcpSessions[session->GetRemoteNetAddress()] = session;
	}

	void Service::ReleaseTcpSession(const std::shared_ptr<TcpSession>& session)
	{
		WRITE_LOCK

		JAM_ASSERT(m_tcpSessions.erase(session->GetRemoteNetAddress()) != 0);
		m_tcpSessionCount--;
	}

	void Service::RegisterUdpSession(const std::shared_ptr<UdpSession>& session)
	{
		WRITE_LOCK

		auto addr = session->GetRemoteNetAddress();
		if (m_udpSessions.contains(addr))
			return;

		m_udpSessionCount++;
		m_udpSessions[addr] = session;
	}

	void Service::ReleaseUdpSession(const std::shared_ptr<UdpSession>& session)
	{
		WRITE_LOCK

		JAM_ASSERT(m_udpSessions.erase(session->GetRemoteNetAddress()) != 0);
		m_udpSessionCount--;
	}

	void Service::RegisterHandshakingUdpSession(const std::shared_ptr<UdpSession>& session)
	{
		WRITE_LOCK

		auto addr = session->GetRemoteNetAddress();
		if (m_handshakingUdpSessions.contains(addr)) return;

		m_handshakingUdpSessions[addr] = session;
	}

	void Service::ReleaseHandshakingUdpSession(const std::shared_ptr<UdpSession>& session)
	{
		WRITE_LOCK

		JAM_ASSERT(m_handshakingUdpSessions.erase(session->GetRemoteNetAddress()) != 0)
	}

	void Service::CompleteUdpHandshake(const NetAddress& from)
	{
		WRITE_LOCK

		auto it = m_handshakingUdpSessions.find(from);
		if (it != m_handshakingUdpSessions.end())
		{
			if (m_udpSessions.contains(from) == false)
			{
				m_udpSessions[from] = it->second;
				m_udpSessionCount++;
			}
			m_handshakingUdpSessions.erase(it);
		}
	}

	std::shared_ptr<UdpSession> Service::FindSessionInConnected(const NetAddress& from)
	{
		READ_LOCK
		auto it = m_udpSessions.find(from);
		if (it != m_udpSessions.end())
			return it->second;
		return nullptr;
	}

	std::shared_ptr<UdpSession> Service::FindSessionInHandshaking(const NetAddress& from)
	{
		READ_LOCK
		auto it = m_handshakingUdpSessions.find(from);
		if (it != m_handshakingUdpSessions.end())
			return it->second;
		return nullptr;
	}

	void Service::ProcessUdpSession(const NetAddress& from, int32 numOfBytes, RecvBuffer& recvBuffer)
	{
		std::shared_ptr<UdpSession> session;

		{
			WRITE_LOCK
			if (auto it = m_udpSessions.find(from); it != m_udpSessions.end())
				session = it->second;
			else if (auto ht = m_handshakingUdpSessions.find(from); ht != m_handshakingUdpSessions.end())
				session = ht->second;
		}

		if (!session)
		{
			WRITE_LOCK
			session = static_pointer_cast<UdpSession>(CreateSession(eProtocolType::UDP));
			if (!session) return;
			session->SetRemoteNetAddress(from);
			m_handshakingUdpSessions[from] = session;
		}

		session->ProcessRecv(numOfBytes, recvBuffer);
	}


	void Service::PumpIocpBatch(uint32 maxBatch)
	{
		if (!m_iocpCore) return;

		for (uint32 i = 0; i < maxBatch; ++i)
		{
			if (!m_iocpCore->Dispatch(0))
				break;
		}
	}

	void Service::ScheduleIocpPump(uint64 interval_ns, uint32 batch)
	{
		m_periodicHandle = GlobalExecutor::Instance().ScheduleFixedRate(
			Job([this, batch]()
				{
					if (!m_running.load(std::memory_order_relaxed))
						return;
					PumpIocpBatch(batch);
				}),
			GlobalExecutor::PeriodicOptions{
				.period_ns = interval_ns,
				.initialDelay_ns = 0,
				.maxCatchUp = 2,
				.name = "Svc.IOCP.Pump"
			}
		);
	}


	ClientService::ClientService(const ServiceConfig& config) : Service(config)
	{
		m_type = eServiceType::CLIENT;
	}

	bool ClientService::Start()
	{
		if (!HasUdpFactory())
			return false;

		m_running.store(true, std::memory_order_relaxed);

		ScheduleIocpPump(200'000_ns, 256); 

		m_udpRouter = std::make_shared<UdpRouter>();
		if (m_udpRouter->Start(this) == false)
			return false;

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

		m_running.store(true, std::memory_order_relaxed);

		ScheduleIocpPump(200'000_ns, 256);

		bool startedAny = false;

		if (HasTcpFactory() && GetLocalTcpNetAddress().IsValid())
		{
			m_listener = std::make_shared<TcpListener>();
			if (m_listener->StartAccept(this) == false)
				return false;
			startedAny = true;
		}

		if (HasUdpFactory() && GetLocalUdpNetAddress().IsValid())
		{
			m_udpRouter = std::make_shared<UdpRouter>();
			if (m_udpRouter->Start(this) == false)
				return false;
			startedAny = true;
		}

		if (!startedAny)
			return false;

		return true;
	}
}
