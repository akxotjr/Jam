#include "pch.h"
#include "jamnet/core/net/TcpListener.h"

#include "jamnet/core/net/IocpEvent.h"
#include "jamnet/core/net/SocketUtils.h"
#include "jamnet/core/net/TcpSession.h"
#include "jamnet/core/net/Service.h"

namespace jam::net
{
	/*--------------
		TcpListener
	---------------*/

	TcpListener::~TcpListener()
	{
		SocketUtils::Close(m_socket);
	}

	bool TcpListener::StartAccept(Service* service)
	{
		m_service = service;
		if (!m_service) return false;

		m_socket = SocketUtils::CreateSocket(eProtocolType::TCP);
		if (m_socket == INVALID_SOCKET)
			return false;

		if (m_service->GetIocpCore()->Register(shared_from_this()) == false)
			return false;

		if (SocketUtils::SetReuseAddress(m_socket, true) == false)
			return false;

		if (SocketUtils::SetLinger(m_socket, 0, 0) == false)
			return false;

		if (SocketUtils::Bind(m_socket, m_service->GetLocalTcpNetAddress()) == false)
			return false;

		if (SocketUtils::Listen(m_socket) == false)
			return false;

		for (int32 i = 0; i < 4; i++)
		{
			auto ev = std::make_unique<TcpAcceptEvent>();
			ev->m_owner = shared_from_this();
			RegisterAccept(ev.get());

			m_events.push_back(std::move(ev));
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

	void TcpListener::Dispatch(IocpEvent* iocpEvent, int32 /*numOfBytes*/)
	{
		JAM_ASSERT(iocpEvent->m_eventType == eEventType::TcpAccept);

		TcpAcceptEvent* acceptEvent = static_cast<TcpAcceptEvent*>(iocpEvent);
		ProcessAccept(acceptEvent);
	}

	void TcpListener::RegisterAccept(TcpAcceptEvent* acceptEvent)
	{
		std::shared_ptr<TcpSession> session = static_pointer_cast<TcpSession>(m_service->CreateSession(eProtocolType::TCP));

		acceptEvent->Init();
		acceptEvent->session = session;

		DWORD bytesReceived = 0;

		BYTE* initialBuf = acceptEvent->acceptBuf.data();
		const DWORD initialLen = 0; // 초기 payload 수신 안 함

		if (false == SocketUtils::AcceptEx(m_socket, session->GetSocket(), initialBuf, initialLen, TcpAcceptEvent::kAddrLen, TcpAcceptEvent::kAddrLen, OUT &bytesReceived, static_cast<LPOVERLAPPED>(acceptEvent)))
		{
			const int32 errorCode = ::WSAGetLastError();
			if (errorCode != WSA_IO_PENDING)
			{
				RegisterAccept(acceptEvent);
			}
		}
	}

	void TcpListener::ProcessAccept(TcpAcceptEvent* acceptEvent)
	{
		std::shared_ptr<TcpSession> session = acceptEvent->session;

		// SO_UPDATE_ACCEPT_CONTEXT
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
