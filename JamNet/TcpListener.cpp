#include "pch.h"
#include "TcpListener.h"

namespace jam::net
{
	/*--------------
		TcpListener
	---------------*/

	TcpListener::~TcpListener()
	{
		SocketUtils::Close(m_socket);

		for (AcceptEvent* acceptEvent : m_acceptEvents)
		{
			utils::memory::xdelete(acceptEvent);
		}
	}

	bool TcpListener::StartAccept(Sptr<Service> service)
	{
		m_service = service;
		if (m_service.lock() == nullptr)
			return false;


		m_socket = SocketUtils::CreateSocket(eProtocolType::TCP);
		if (m_socket == INVALID_SOCKET)
			return false;

		if (m_service.lock()->GetIocpCore()->Register(shared_from_this()) == false)
			return false;

		if (SocketUtils::SetReuseAddress(m_socket, true) == false)
			return false;

		if (SocketUtils::SetLinger(m_socket, 0, 0) == false)
			return false;

		if (SocketUtils::Bind(m_socket, m_service.lock()->GetLocalTcpNetAddress()) == false)
			return false;

		if (SocketUtils::Listen(m_socket) == false)
			return false;

		const int32 acceptCount = m_service.lock()->GetMaxTcpSessionCount();	// todo
		for (int32 i = 0; i < acceptCount; i++)
		{
			AcceptEvent* acceptEvent = utils::memory::xnew<AcceptEvent>();
			acceptEvent->m_owner = shared_from_this();
			m_acceptEvents.push_back(acceptEvent);
			RegisterAccept(acceptEvent);
		}

		return true;
	}

	void TcpListener::CloseSocket()
	{
		SocketUtils::Close(m_socket);
	}

	HANDLE TcpListener::GetHandle()
	{
		return reinterpret_cast<HANDLE>(m_socket);
	}

	void TcpListener::Dispatch(IocpEvent* iocpEvent, int32 numOfBytes)
	{
		ASSERT_CRASH(iocpEvent->m_eventType == eEventType::Accept);
		AcceptEvent* acceptEvent = static_cast<AcceptEvent*>(iocpEvent);
		ProcessAccept(acceptEvent);
	}

	void TcpListener::RegisterAccept(AcceptEvent* acceptEvent)
	{
		Sptr<TcpSession> session = static_pointer_cast<TcpSession>(m_service.lock()->CreateSession(eProtocolType::TCP));

		acceptEvent->Init();
		acceptEvent->session = session;

		DWORD bytesReceived = 0;
		if (false == SocketUtils::AcceptEx(m_socket, session->GetSocket(), session->m_recvBuffer.WritePos(), 0, sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, OUT &bytesReceived, static_cast<LPOVERLAPPED>(acceptEvent)))
		{
			const int32 errorCode = ::WSAGetLastError();
			if (errorCode != WSA_IO_PENDING)
			{
				//RegisterAccept(acceptEvent);
			}
		}
	}

	void TcpListener::ProcessAccept(AcceptEvent* acceptEvent)
	{
		Sptr<TcpSession> session = acceptEvent->session;

		if (false == SocketUtils::SetUpdateAcceptSocket(session->GetSocket(), m_socket))
		{
			RegisterAccept(acceptEvent);
			return;
		}

		SOCKADDR_IN sockAddress = {};
		int32 sizeOfSockAddr = sizeof(sockAddress);
		if (SOCKET_ERROR == ::getpeername(session->GetSocket(), OUT reinterpret_cast<SOCKADDR*>(&sockAddress), &sizeOfSockAddr))
		{
			RegisterAccept(acceptEvent);
			return;
		}

		session->SetRemoteNetAddress(NetAddress(sockAddress));

		session->ProcessConnect();

		RegisterAccept(acceptEvent);
	}
}
