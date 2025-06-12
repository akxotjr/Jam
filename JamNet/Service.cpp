#include "pch.h"
#include "Service.h"

namespace jam::net
{
	/*--------------
		 Service
		---------------*/

	Service::Service(TransportConfig config, int32 maxTcpSessionCount, int32 maxUdpSessionCount)
		: m_config(config), m_maxTcpSessionCount(maxTcpSessionCount), m_maxUdpSessionCount(maxUdpSessionCount)
	{
		m_iocpCore = std::make_unique<IocpCore>();
	}

	Service::~Service()
	{
		CloseService();
	}


	void Service::CloseService()
	{
		// TODO
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

		return session;
	}

	void Service::AddTcpSession(Sptr<TcpSession> session)
	{
		WRITE_LOCK

		auto addr = session->GetRemoteNetAddress();
		if (m_tcpSessions.contains(addr))
			return;

		m_tcpSessionCount++;
		m_tcpSessions[session->GetRemoteNetAddress()] = session;
	}

	void Service::ReleaseTcpSession(Sptr<TcpSession> session)
	{
		WRITE_LOCK

		ASSERT_CRASH(m_tcpSessions.erase(session->GetRemoteNetAddress()) != 0);
		m_tcpSessionCount--;
	}

	void Service::AddUdpSession(Sptr<UdpSession> session)
	{
		WRITE_LOCK

		auto addr = session->GetRemoteNetAddress();
		if (m_udpSessions.contains(addr))
			return;

		m_udpSessionCount++;
		m_udpSessions[addr] = session;
	}

	void Service::ReleaseUdpSession(Sptr<UdpSession> session)
	{
		WRITE_LOCK

		ASSERT_CRASH(m_udpSessions.erase(session->GetRemoteNetAddress()) != 0);
		m_udpSessionCount--;
	}

	void Service::AddHandshakingUdpSession(Sptr<UdpSession> session)
	{
		WRITE_LOCK

		m_handshakingUdpSessions[session->GetRemoteNetAddress()] = session;
	}


	void Service::CompleteUdpHandshake(const NetAddress& from)
	{
		WRITE_LOCK

		auto it = m_handshakingUdpSessions.find(from);
		if (it != m_handshakingUdpSessions.end())
		{
			AddUdpSession(it->second);
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











	ClientService::ClientService(TransportConfig config, int32 maxTcpSessionCount, int32 maxUdpSessionCount)
		: Service(config, maxTcpSessionCount, maxUdpSessionCount)
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

		m_udpRouter = utils::memory::MakeShared<UdpRouter>();

		if (m_udpRouter->Start(shared_from_this()) == false)
			return false;

		return true;
	}







	ServerService::ServerService(TransportConfig config, int32 maxTcpSessionCount, int32 maxUdpSessionCount)
		: Service(config, maxTcpSessionCount, maxUdpSessionCount)
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

		m_listener = utils::memory::MakeShared<TcpListener>();
		m_udpRouter = utils::memory::MakeShared<UdpRouter>();

		if (m_listener->StartAccept(shared_from_this()) == false)
			return false;

		if (m_udpRouter->Start(shared_from_this()) == false)
			return false;

		return true;
	}
}
