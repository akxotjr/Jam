#include "pch.h"
#include "Service.h"

#include <ranges>

#include "Clock.h"
#include "SessionUpdateSystem.h"

namespace jam::net
{
	Service::Service(ServiceConfig config) : m_config(config)
	{
		m_iocpCore = std::make_unique<IocpCore>();
		m_globalExecutor = std::make_unique<utils::exec::GlobalExecutor>(m_config.geConfig);
	}

	Service::~Service()
	{
		Service::CloseService();
	}

	void Service::Init()
	{
		if (m_config.routeSeed.k0 == 0 && m_config.routeSeed.k1 == 0) 
		{
			m_config.routeSeed = utils::exec::RandomSeed();
			m_routing = utils::exec::RoutingPolicy(m_config.routeSeed);
		}

		m_globalExecutor->Init();



		// temp
		auto& shards = m_globalExecutor->GetShards();
		for (auto& shard : shards)
		{
			shard->Local().systems.push_back(&SessionUpdateSystem);
		}
	}


	void Service::CloseService()
	{
		m_running.store(false, std::memory_order_relaxed);
		if (m_globalExecutor)
		{
			m_globalExecutor->Stop();
			m_globalExecutor->Join();
		}
	}


	void Service::StartUpdateLoop(uint64 period_ns)
	{
		//m_running.store(true);
		//m_lastUpdateTick = utils::Clock::Instance().GetCurrentTick();
		auto& shards = m_globalExecutor->GetShards();
		for (auto& shard : shards) {
			auto postTick = [this, &shard, period_ns]() {
				const uint64_t now = utils::Clock::Instance().NowNs();
				shard->Tick(now, period_ns);
				// 재귀 예약
				utils::exec::GlobalExecutor::PostAfter(utils::job::Job([this, &shard, period_ns]() { /* same lambda body */ }), period_ns);
				};
			utils::exec::GlobalExecutor::PostAfter(utils::job::Job(postTick), period_ns);
		}
	}

	void Service::Update()
	{
		//auto self = static_pointer_cast<Service>(shared_from_this());
		//m_globalExecutor->Post(utils::job::Job([self] {
		//		self->ProcessUpdate();
		//	}));
	}


	Sptr<Session> Service::CreateSession(eProtocolType protocol)
	{
		Sptr<Session> session = nullptr;

		switch (protocol)
		{
		case eProtocolType::TCP:
			session = m_tcpSessionFactory();
			if (m_iocpCore->Register(session) == false)
				return nullptr;
			session->SetRemoteNetAddress(GetRemoteTcpNetAddress());
			break;
		case eProtocolType::UDP:
			session = m_udpSessionFactory();
			session->SetRemoteNetAddress(GetRemoteUdpNetAddress());
			break;
		}

		session->SetService(shared_from_this());

		auto dir = m_globalExecutor->GetDirectory();
		ASSERT_CRASH(dir != nullptr);

		const uint64 connSalt = reinterpret_cast<uint64>(session.get()) ^ utils::Clock::Instance().NowNs();
		const utils::exec::RouteKey tempKey = m_routing.KeyForSession(connSalt); // PerUser 정책의 해시를 임시키에도 재사용

		session->AttachEndpoint(*dir, tempKey);

		return session;
	}

	void Service::RegisterTcpSession(const Sptr<TcpSession>& session)
	{
		WRITE_LOCK

		auto addr = session->GetRemoteNetAddress();
		if (m_tcpSessions.contains(addr))
			return;

		m_tcpSessionCount++;
		m_tcpSessions[session->GetRemoteNetAddress()] = session;
	}

	void Service::ReleaseTcpSession(const Sptr<TcpSession>& session)
	{
		WRITE_LOCK

		ASSERT_CRASH(m_tcpSessions.erase(session->GetRemoteNetAddress()) != 0);
		m_tcpSessionCount--;
	}

	void Service::RegisterUdpSession(const Sptr<UdpSession>& session)
	{
		WRITE_LOCK

		auto addr = session->GetRemoteNetAddress();
		if (m_udpSessions.contains(addr))
			return;

		m_udpSessionCount++;
		m_udpSessions[addr] = session;
	}

	void Service::ReleaseUdpSession(const Sptr<UdpSession>& session)
	{
		WRITE_LOCK

		ASSERT_CRASH(m_udpSessions.erase(session->GetRemoteNetAddress()) != 0);
		m_udpSessionCount--;
	}

	void Service::CompleteUdpHandshake(const NetAddress& from)
	{
		WRITE_LOCK

		auto it = m_handshakingUdpSessions.find(from);
		if (it != m_handshakingUdpSessions.end())
		{
			RegisterUdpSession(it->second);
			m_handshakingUdpSessions.erase(it);
		}
	}

	Sptr<UdpSession> Service::FindSessionInConnected(const NetAddress& from)
	{
		if (m_udpSessions.contains(from))
			return m_udpSessions[from];

		return nullptr;
	}

	Sptr<UdpSession> Service::FindSessionInHandshaking(const NetAddress& from)
	{
		if (m_handshakingUdpSessions.contains(from))
			return m_handshakingUdpSessions[from];

		return nullptr;
	}

	Sptr<UdpSession> Service::CreateAndRegisterToHandshaking(const NetAddress& from)
	{
		Sptr<UdpSession> newSession = static_pointer_cast<UdpSession>(CreateSession(eProtocolType::UDP));
		newSession->SetRemoteNetAddress(from);

		m_handshakingUdpSessions[from] = newSession;

		return newSession;
	}

	void Service::ProcessUdpSession(const NetAddress& from, int32 numOfBytes, RecvBuffer recvBuffer)
	{
		Sptr<UdpSession> session = FindSessionInConnected(from);
		if (!session)
		{
			session = FindSessionInHandshaking(from);
			if (!session)
				session = CreateAndRegisterToHandshaking(from);
		}

		session->ProcessRecv(numOfBytes, recvBuffer);
	}

	void Service::ProcessUpdate()
	{
		if (!m_running.load())
			return;

		uint64 currentTick = utils::Clock::Instance().GetCurrentTick();

		if (currentTick - m_lastUpdateTick < UPDATE_INTERVAL_MS)
			return;

		m_lastUpdateTick = currentTick;

		{
			//READ_LOCK  ????
				for (auto& session : m_tcpSessions | views::values)
				{
					if (session && session->IsConnected())
					{
						session->Update();
					}
				}
		}

		{
			READ_LOCK
				for (auto& session : m_udpSessions | views::values)
				{
					if (session && session->IsConnected())
					{
						session->Update();
					}
				}
		}

		{
			READ_LOCK
				for (auto& session : m_handshakingUdpSessions | views::values)
				{
					if (session)
					{
						session->Update();
					}
				}
		}

		m_globalExecutor->PostAfter(utils::job::Job<Service>(shared_from_this(), ProcessUpdate()), 100);
	}


	ClientService::ClientService(ServiceConfig config) : Service(config)
	{
		m_peer = ePeerType::Client;
	}

	ClientService::~ClientService()
	{
	}

	bool ClientService::Start()
	{
		if (CanStart() == false)
			return false;

		if (m_globalExecutor)
			m_globalExecutor->Start();

		m_udpRouter = utils::memory::MakeShared<UdpRouter>();

		if (m_udpRouter->Start(shared_from_this()) == false)
			return false;

		StartUpdateLoop();

		return true;
	}



	ServerService::ServerService(ServiceConfig config) : Service(config)
	{
		m_peer = ePeerType::Server;
	}

	ServerService::~ServerService()
	{
	}

	bool ServerService::Start()
	{
		if (CanStart() == false)
			return false;

		if (m_globalExecutor)
			m_globalExecutor->Start();

		m_listener = utils::memory::MakeShared<TcpListener>();
		m_udpRouter = utils::memory::MakeShared<UdpRouter>();

		if (m_listener->StartAccept(shared_from_this()) == false)
			return false;

		if (m_udpRouter->Start(shared_from_this()) == false)
			return false;

		//m_workerPool->Run();
		StartUpdateLoop();

		return true;
	}
}
