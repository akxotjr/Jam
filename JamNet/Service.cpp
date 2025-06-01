#include "pch.h"
#include "Service.h"

namespace jam::net
{
	/*--------------
 Service
---------------*/

	Service::Service(TransportConfig config, Sptr<IocpCore> core, int32 maxTcpSessionCount, int32 maxUdpSessionCount)
		: m_config(config), m_iocpCore(core), m_maxTcpSessionCount(maxTcpSessionCount), m_maxUdpSessionCount(maxUdpSessionCount)
	{
	}

	Service::~Service()
	{
		CloseService();
	}

	bool Service::Start()
	{
		if (CanStart() == false)
			return false;

		m_listener = utils::memory::MakeShared<TcpListener>();
		if (m_listener == nullptr)
			return false;

		Sptr<Service> service = static_pointer_cast<Service>(shared_from_this());
		if (m_listener->StartAccept(service) == false)
			return false;

		if (m_udpReceiver == nullptr)
			return false;

		if (m_udpReceiver->Start(service) == false)
			return false;

		return true;
	}

	void Service::CloseService()
	{
		// TODO
	}

	void Service::Broadcast(Sptr<SendBuffer> sendBuffer)
	{
		//WRITE_LOCK;
		//for (const auto& session : _sessions)
		//{
		//	session->Send(sendBuffer);
		//}
	}

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
		return session;
	}

	void Service::AddTcpSession(Sptr<TcpSession> session)
	{
		WRITE_LOCK

		m_tcpSessionCount++;
		m_tcpSessions.insert(session);
	}

	void Service::ReleaseTcpSession(Sptr<TcpSession> session)
	{
		WRITE_LOCK

		ASSERT_CRASH(m_tcpSessions.erase(session) != 0);
		m_tcpSessionCount--;
	}

	void Service::AddUdpSession(Sptr<UdpSession> session)
	{
		WRITE_LOCK

		m_udpSessionCount++;
		m_udpSessions.insert(session);
	}

	void Service::ReleaseUdpSession(Sptr<UdpSession> session)
	{
		WRITE_LOCK

		ASSERT_CRASH(m_udpSessions.erase(session) != 0);
		m_udpSessionCount--;
	}

	Sptr<UdpSession> Service::FindOrCreateUdpSession(const NetAddress& from)
	{
		WRITE_LOCK

		for (auto& session : m_udpSessions)
		{
			if (session->GetRemoteNetAddress() == from)
				return session;
		}

		auto it = m_pendingUdpSessions.find(from);
		if (it != m_pendingUdpSessions.end())
		{
			return it->second;
		}

		auto newSession = static_pointer_cast<UdpSession>(CreateSession(EProtocolType::UDP));
		if (newSession == nullptr)
			return nullptr;

		newSession->SetRemoteNetAddress(from);

		m_pendingUdpSessions[from] = newSession;

		newSession->ProcessConnect();

		return newSession;
	}

	void Service::CompleteUdpHandshake(const NetAddress& from)
	{
		WRITE_LOCK

		auto it = m_pendingUdpSessions.find(from);
		if (it != m_pendingUdpSessions.end())
		{
			AddUdpSession(it->second);
			m_pendingUdpSessions.erase(it);
		}
	}
}
