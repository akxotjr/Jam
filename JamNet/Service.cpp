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

	//void Service::Broadcast(Sptr<SendBuffer> sendBuffer)
	//{
	//	//WRITE_LOCK;
	//	//for (const auto& session : _sessions)
	//	//{
	//	//	session->Send(sendBuffer);
	//	//}
	//}

	Sptr<Session> Service::CreateSession(EProtocolType protocol)
	{
		Sptr<Session> session = nullptr;
		if (protocol == EProtocolType::TCP)
		{
			session = m_tcpSessionFactory();
			if (m_iocpCore->Register(session) == false)
				return nullptr;
		}
		else if (protocol == EProtocolType::UDP)
		{
			session = m_udpSessionFactory();
		}

		session->SetService(shared_from_this());

		if (protocol == EProtocolType::TCP)
			session->SetRemoteNetAddress(GetRemoteTcpNetAddress());
		else if (protocol == EProtocolType::UDP)
			session->SetRemoteNetAddress(GetRemoteUdpNetAddress());

		return session;
	}

	void Service::AddTcpSession(Sptr<TcpSession> session)
	{
		WRITE_LOCK

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

		m_udpSessionCount++;
		m_udpSessions[session->GetRemoteNetAddress()] = session;
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

	//Sptr<UdpSession> Service::FindOrCreateUdpSession(const NetAddress& from)
	//{
	//	WRITE_LOCK

	//	if (m_udpSessions.contains(from))
	//		return m_udpSessions[from];

	//	if (m_pendingUdpSessions.contains(from))
	//		return m_pendingUdpSessions[from];

	//	auto newSession = static_pointer_cast<UdpSession>(CreateSession(EProtocolType::UDP));
	//	if (newSession == nullptr)
	//		return nullptr;

	//	newSession->SetRemoteNetAddress(from);

	//	m_pendingUdpSessions[from] = newSession;

	//	//newSession->ProcessConnect();

	//	return newSession;
	//}

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

	Sptr<UdpSession> Service::FindUpdSession(const NetAddress& from)
	{
		if (m_udpSessions.contains(from))
			return m_udpSessions[from];

		return nullptr;
	}

	void Service::ProcessUdpSession(const NetAddress& from, int32 numOfBytes, RecvBuffer recvBuffer)
	{
		if (m_udpSessions.contains(from))
		{
			m_udpSessions[from]->ProcessRecv(numOfBytes, recvBuffer);
			return;
		}

		if (m_handshakingUdpSessions.contains(from))
		{
			m_handshakingUdpSessions[from]->ProcessHandshake(numOfBytes, recvBuffer);	// todo
			return;
		}

		auto newSession = static_pointer_cast<UdpSession>(CreateSession(EProtocolType::UDP));
		newSession->SetRemoteNetAddress(from);
		m_handshakingUdpSessions[from] = newSession;
		newSession->ProcessHandshake(numOfBytes, recvBuffer);
	}

	ClientService::ClientService(TransportConfig config, int32 maxTcpSessionCount, int32 maxUdpSessionCount)
		: Service(config, maxTcpSessionCount, maxUdpSessionCount)
	{
		m_peer = EPeerType::Client;
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
		m_peer = EPeerType::Server;
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
